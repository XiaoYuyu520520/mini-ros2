// viz_feed - the data plane for `viz.exe`.
//
// The GUI is a Go/Fyne program and cannot open the shim's shared-memory
// channels without re-implementing the Windows named mutex/event protocol and
// every message layout. This process does that the same way the nav2 nodes do:
// it is an ordinary shim `rclcpp::Node` with typed subscriptions, plus a
// `tf2_ros::Buffer` whose transform listener turns every message into the
// view's fixed frame before it leaves this process.
//
// The GUI starts this program as a child and talks to it over stdin/stdout:
//
//   uint32 little-endian payload length
//   uint8  kind (1 = JSON, 2 = reserved binary)
//   payload
//
// stdout carries only frames; logs go to stderr. On Windows both descriptors
// are switched to binary mode, otherwise the CRT would turn every 0x0A in a
// binary payload into 0x0D 0x0A.
//
// The command set and the payload shapes are documented in viz/README.md. The
// short version:
//
//   {"op":"hello"}                       -> protocol version, namespace
//   {"op":"topics"} / services / actions -> the shim graph
//   {"op":"frames"}                      -> TF frame names
//   {"op":"subscribe","topic":...}       -> stream `frame` events
//   {"op":"unsubscribe","topic":...}
//   {"op":"set_fixed_frame","frame":...}
//   {"op":"publish_pose","topic":...}    -> /goal_pose or /initialpose
//   {"op":"shutdown"}
//
// A shim channel has exactly one writer, and this process holds the write end
// of every channel it creates (including the two tool publishers below). So
// `publish_pose` writes straight into the channel it owns instead of asking
// the node to publish: a second writer on the same name would truncate the
// first one's ring. The same flag is set for `/goal_pose` and `/initialpose`,
// which is why nav2's own `ros2 topic pub` for those topics would collide -
// run the viewer instead, or point it at a different tool topic.
//
// A subscription is created only after its type is known. The GUI may ask for
// a topic that has no publisher yet (the common "start the viewer first"
// order), so the command is parked and retried until the graph answers - the
// same behaviour as `ros2 topic echo`.
//
// Message decoding is typed: the generated headers under
// nav2/ros2_shim/generated/include are included directly and the shim's own
// subscription callback hands over the struct. There is no text dump and no
// second parser: `describe_shim` is for humans and truncates maps.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "nlohmann/json.hpp"

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "geometry_msgs/msg/pose_with_covariance_stamped.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "nav_msgs/msg/path.hpp"
#include "rclcpp/rclcpp.hpp"
#include "ros2_bridge/shm_topic.hpp"
#include "ros2_shim/graph.hpp"
#include "ros2_shim/serialization.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"
#include "tf2/exceptions.hpp"
#include "tf2_ros/buffer.hpp"
#include "tf2_ros/transform_listener.hpp"
#include "tf2_msgs/msg/tf_message.hpp"

namespace
{

using json = nlohmann::json;

std::atomic<bool> g_running{true};

void handleSignal(int)
{
  g_running.store(false);
}

// ---------------------------------------------------------------- framing

constexpr std::uint8_t kJson = 1;
constexpr std::uint8_t kBinary = 2;
constexpr std::uint32_t kMaxFrame = 64u << 20;

void setBinaryStdio()
{
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
}

bool readFrame(std::uint8_t & kind, std::vector<std::uint8_t> & payload)
{
  std::uint8_t header[5];
  if (std::fread(header, 1, sizeof(header), stdin) != sizeof(header)) {
    return false;
  }
  const std::uint32_t length =
    static_cast<std::uint32_t>(header[0]) |
    (static_cast<std::uint32_t>(header[1]) << 8) |
    (static_cast<std::uint32_t>(header[2]) << 16) |
    (static_cast<std::uint32_t>(header[3]) << 24);
  kind = header[4];
  if (length == 0 || length > kMaxFrame) {
    return false;
  }
  payload.resize(length);
  if (std::fread(payload.data(), 1, length, stdin) != length) {
    return false;
  }
  return true;
}

class Out
{
public:
  void json(const json & value)
  {
    const std::string body = value.dump();
    write(kJson, reinterpret_cast<const std::uint8_t *>(body.data()), body.size());
  }

private:
  void write(std::uint8_t kind, const std::uint8_t * data, std::size_t size)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint32_t length = static_cast<std::uint32_t>(size);
    std::uint8_t header[5] = {
      static_cast<std::uint8_t>(length & 0xff),
      static_cast<std::uint8_t>((length >> 8) & 0xff),
      static_cast<std::uint8_t>((length >> 16) & 0xff),
      static_cast<std::uint8_t>((length >> 24) & 0xff),
      kind,
    };
    std::fwrite(header, 1, sizeof(header), stdout);
    std::fwrite(data, 1, size, stdout);
    std::fflush(stdout);
  }

  std::mutex mutex_;
};

// The writer never runs on the node's spin thread. A callback only replaces
// the latest frame for its topic; a UI that stops reading the pipe can slow
// this thread down, and the shared-memory ring then drops old samples, but TF
// and the other topics keep running.
class FrameHub
{
public:
  explicit FrameHub(Out & out) : out_(out) {}

  void set(const std::string & topic, std::string body)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      latest_[topic] = std::move(body);
    }
    cv_.notify_one();
  }

  void run()
  {
    while (true) {
      std::map<std::string, std::string> pending;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] {return !running_ || !latest_.empty();});
        if (!running_) {
          return;
        }
        pending.swap(latest_);
      }
      for (const auto & entry : pending) {
        out_.json(json::parse(entry.second));
      }
    }
  }

  void stop()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      running_ = false;
    }
    cv_.notify_all();
  }

private:
  Out & out_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::map<std::string, std::string> latest_;
  bool running_ = true;
};

std::string base64(const std::uint8_t * data, std::size_t size)
{
  static const char * alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((size + 2) / 3) * 4);
  std::size_t i = 0;
  while (i + 3 <= size) {
    const std::uint32_t v =
      (static_cast<std::uint32_t>(data[i]) << 16) |
      (static_cast<std::uint32_t>(data[i + 1]) << 8) |
      static_cast<std::uint32_t>(data[i + 2]);
    out.push_back(alphabet[(v >> 18) & 0x3f]);
    out.push_back(alphabet[(v >> 12) & 0x3f]);
    out.push_back(alphabet[(v >> 6) & 0x3f]);
    out.push_back(alphabet[v & 0x3f]);
    i += 3;
  }
  if (i < size) {
    std::uint32_t v = static_cast<std::uint32_t>(data[i]) << 16;
    if (i + 1 < size) {
      v |= static_cast<std::uint32_t>(data[i + 1]) << 8;
    }
    out.push_back(alphabet[(v >> 18) & 0x3f]);
    out.push_back(alphabet[(v >> 12) & 0x3f]);
    out.push_back(i + 1 < size ? alphabet[(v >> 6) & 0x3f] : '=');
    out.push_back('=');
  }
  return out;
}

// -------------------------------------------------------------- quaternions

struct Quat
{
  double x = 0, y = 0, z = 0, w = 1;
};

struct Vec3
{
  double x = 0, y = 0, z = 0;
};

Quat mul(const Quat & a, const Quat & b)
{
  return Quat{
    a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
    a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
  };
}

Vec3 rotate(const Quat & q, const Vec3 & v)
{
  const Vec3 u{q.x, q.y, q.z};
  const double s = q.w;
  const Vec3 uv{
    u.y * v.z - u.z * v.y,
    u.z * v.x - u.x * v.z,
    u.x * v.y - u.y * v.x,
  };
  const Vec3 uuv{
    u.y * uv.z - u.z * uv.y,
    u.z * uv.x - u.x * uv.z,
    u.x * uv.y - u.y * uv.x,
  };
  return Vec3{
    v.x + 2.0 * (s * uv.x + uuv.x),
    v.y + 2.0 * (s * uv.y + uuv.y),
    v.z + 2.0 * (s * uv.z + uuv.z),
  };
}

double yawOf(const Quat & q)
{
  const double siny = 2.0 * (q.w * q.z + q.x * q.y);
  const double cosy = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
  return std::atan2(siny, cosy);
}

Quat fromYaw(double yaw)
{
  return Quat{0, 0, std::sin(yaw / 2.0), std::cos(yaw / 2.0)};
}

struct Xform
{
  bool ok = false;
  Vec3 t;
  Quat q;
};

Xform identity()
{
  return Xform{true, Vec3{}, Quat{}};
}

Xform fromMsg(const geometry_msgs::msg::TransformStamped & msg)
{
  Xform out;
  out.ok = true;
  out.t = Vec3{
    msg.transform.translation.x, msg.transform.translation.y, msg.transform.translation.z};
  out.q = Quat{
    msg.transform.rotation.x, msg.transform.rotation.y, msg.transform.rotation.z,
    msg.transform.rotation.w};
  return out;
}

std::int64_t stampNs(const builtin_interfaces::msg::Time & stamp)
{
  return static_cast<std::int64_t>(stamp.sec) * 1000000000LL +
         static_cast<std::int64_t>(stamp.nanosec);
}

const char * kindForType(const std::string & type)
{
  if (type == "nav_msgs/msg/OccupancyGrid") {return "occupancy_grid";}
  if (type == "sensor_msgs/msg/LaserScan") {return "laser_scan";}
  if (type == "nav_msgs/msg/Path") {return "path";}
  if (type == "geometry_msgs/msg/PoseStamped") {return "pose";}
  if (type == "geometry_msgs/msg/PoseWithCovarianceStamped") {return "pose_covariance";}
  if (type == "nav_msgs/msg/Odometry") {return "odometry";}
  return "";
}

// ------------------------------------------------------------ subscription

class Sidecar;

struct Subscription
{
  virtual ~Subscription() = default;
  virtual void emit() = 0;
  std::string topic;
  std::string type;
  bool transient_local = false;
  double max_hz = 0;
  bool enabled = true;
  std::chrono::steady_clock::time_point last_emit{};
  std::chrono::steady_clock::time_point last_error{};
};

template<class MessageT>
class TypedSubscription : public Subscription
{
public:
  using Message = MessageT;
  using Shared = std::shared_ptr<const MessageT>;

  TypedSubscription(
    rclcpp::Node::SharedPtr node, const std::string & topic_name, const std::string & type_name,
    bool transient_local_, double max_hz_,
    std::function<void(const MessageT &, const Xform &)> encode,
    std::function<Xform(const MessageT &)> transform,
    std::function<void(json)> error_sink)
  : encode_(std::move(encode)), transform_(std::move(transform)), error_sink_(std::move(error_sink))
  {
    topic = topic_name;
    type = type_name;
    transient_local = transient_local_;
    max_hz = max_hz_;
    rclcpp::QoS qos = transient_local
      ? rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local()
      : rclcpp::QoS(10);
    subscription_ = node->create_subscription<MessageT>(
      topic, qos,
      [this](Shared message) {onMessage(message);});
  }

  void emit() override
  {
    Shared last;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      last = last_;
    }
    if (!last || !enabled) {
      return;
    }
    publish(*last);
  }

private:
  void onMessage(const Shared & message)
  {
    if (!g_running.load() || !enabled) {
      return;
    }
    if (max_hz > 0) {
      const auto now = std::chrono::steady_clock::now();
      const auto period = std::chrono::duration<double>(1.0 / std::max(0.1, max_hz));
      if (last_emit.time_since_epoch().count() != 0 &&
        std::chrono::duration<double>(now - last_emit) < period)
      {
        return;
      }
      last_emit = now;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_ = message;
    }
    publish(*message);
  }

  void publish(const MessageT & message)
  {
    const Xform xform = transform_(message);
    if (!xform.ok) {
      const auto now = std::chrono::steady_clock::now();
      if (last_error.time_since_epoch().count() == 0 ||
        now - last_error > std::chrono::seconds(1))
      {
        last_error = now;
        json event;
        event["event"] = "error";
        event["topic"] = topic;
        event["message"] = "no transform to the fixed frame yet";
        if (error_sink_) {
          error_sink_(std::move(event));
        }
      }
      return;
    }
    encode_(message, xform);
  }

  typename rclcpp::Subscription<MessageT>::SharedPtr subscription_;
  std::mutex mutex_;
  Shared last_;
  std::function<void(const MessageT &, const Xform &)> encode_;
  std::function<Xform(const MessageT &)> transform_;
  std::function<void(json)> error_sink_;
};

// ---------------------------------------------------------------- sidecar

class Sidecar
{
public:
  Sidecar()
  : out_(), hub_(out_)
  {
    node_ = std::make_shared<rclcpp::Node>("viz_feed");
    buffer_ = std::make_shared<tf2_ros::Buffer>(
      node_->get_clock(), tf2::Duration(std::chrono::seconds(10)));
    listener_ = std::make_shared<tf2_ros::TransformListener>(*buffer_, node_);

    // Both tool channels exist from the start: creating the channel is what
    // makes a single publish after a GUI restart reliable (the daemon's uplink
    // and a local nav2 both hold a reader, but a cold GUI would otherwise race
    // the first message). They are advertised here, not through a
    // `rclcpp::Publisher`, because this process writes the ring directly - see
    // the note at the top of the file.
    toolChannel("/goal_pose", "geometry_msgs/msg/PoseStamped");
    toolChannel("/initialpose", "geometry_msgs/msg/PoseWithCovarianceStamped");

    spin_thread_ = std::thread([this] {spinLoop();});
    writer_thread_ = std::thread([this] {hub_.run();});
    frames_thread_ = std::thread([this] {framesLoop();});
  }

  ~Sidecar()
  {
    g_running.store(false);
    hub_.stop();
    if (spin_thread_.joinable()) {spin_thread_.join();}
    if (writer_thread_.joinable()) {writer_thread_.join();}
    if (frames_thread_.joinable()) {frames_thread_.join();}
  }

  void start()
  {
    json hello;
    hello["event"] = "hello";
    hello["protocol"] = 1;
    hello["version"] = "0.1.0";
    const char * ns = std::getenv("NAV2_SHM_NAMESPACE");
    hello["namespace"] = ns ? ns : "";
    hello["fixed_frame"] = fixedFrame();
    out_.json(hello);
  }

  int serve()
  {
    std::uint8_t kind = 0;
    std::vector<std::uint8_t> payload;
    while (g_running.load() && readFrame(kind, payload)) {
      if (kind != kJson) {
        respondError(0, "only JSON command frames are supported");
        continue;
      }
      json command;
      try {
        command = json::parse(payload.begin(), payload.end());
      } catch (const std::exception & error) {
        respondError(0, std::string("bad JSON: ") + error.what());
        continue;
      }
      if (!dispatch(command)) {
        break;
      }
    }
    return 0;
  }

  private:
  // ------------------------------------------------------------- dispatch
  bool dispatch(const json & command)
  {
    const std::uint64_t id = command.value("id", 0ULL);
    const std::string op = command.value("op", "");
    try {
      if (op == "hello") {
        json result;
        result["protocol"] = 1;
        result["version"] = "0.1.0";
        const char * ns = std::getenv("NAV2_SHM_NAMESPACE");
        result["namespace"] = ns ? ns : "";
        result["fixed_frame"] = fixedFrame();
        respond(id, result);
      } else if (op == "ping") {
        respond(id, "pong");
      } else if (op == "topics") {
        respond(id, topicList());
      } else if (op == "services") {
        respond(id, serviceList());
      } else if (op == "actions") {
        respond(id, actionList());
      } else if (op == "nodes") {
        respond(id, nodeList());
      } else if (op == "frames") {
        respond(id, frameList());
      } else if (op == "subscribe") {
        subscribe(command);
        respond(id, true);
      } else if (op == "unsubscribe") {
        unsubscribe(command.value("topic", ""));
        respond(id, true);
      } else if (op == "set_fixed_frame") {
        setFixedFrame(command.value("frame", "map"));
        respond(id, fixedFrame());
      } else if (op == "publish_pose") {
        respond(id, publishPose(command));
      } else if (op == "shutdown") {
        respond(id, true);
        g_running.store(false);
        return false;
      } else {
        respondError(id, "unknown op '" + op + "'");
      }
    } catch (const std::exception & error) {
      respondError(id, error.what());
    }
    return true;
  }

  void respond(std::uint64_t id, const json & result)
  {
    json response;
    response["id"] = id;
    response["ok"] = true;
    response["result"] = result;
    out_.json(response);
  }

  void respondError(std::uint64_t id, const std::string & message)
  {
    json response;
    response["id"] = id;
    response["ok"] = false;
    response["error"] = message;
    out_.json(response);
  }

  // ---------------------------------------------------------------- graph
  static json topicList()
  {
    std::map<std::string, json> rows;
    for (const auto & entry : ros2_shim::graph::list_topics()) {
      json & row = rows[entry.name];
      if (row.is_null()) {
        row = json{{"name", entry.name}, {"type", ""}, {"publishers", 0},
          {"subscribers", 0}, {"sources", json::array()}, {"remote", false}};
      }
      if (row["type"].get<std::string>().empty() || entry.source == "local") {
        row["type"] = entry.type;
      }
      row["publishers"] = row["publishers"].get<int>() + entry.publishers;
      row["subscribers"] = row["subscribers"].get<int>() + entry.subscribers;
      row["sources"].push_back(entry.source);
      if (entry.source.rfind("remote:", 0) == 0) {
        row["remote"] = true;
      }
    }
    json out = json::array();
    for (auto & entry : rows) {
      out.push_back(entry.second);
    }
    return out;
  }

  static json serviceList()
  {
    std::map<std::string, json> rows;
    for (const auto & entry : ros2_shim::graph::list_services()) {
      json & row = rows[entry.name];
      if (row.is_null()) {
        row = json{{"name", entry.name}, {"type", ""},
          {"sources", json::array()}, {"remote", false}};
      }
      if (row["type"].get<std::string>().empty() || entry.source == "local") {
        row["type"] = entry.type;
      }
      row["sources"].push_back(entry.source);
      if (entry.source.rfind("remote:", 0) == 0) {
        row["remote"] = true;
      }
    }
    json out = json::array();
    for (auto & entry : rows) {
      out.push_back(entry.second);
    }
    return out;
  }

  static json actionList()
  {
    std::map<std::string, json> rows;
    for (const auto & entry : ros2_shim::graph::list_actions()) {
      json & row = rows[entry.name];
      if (row.is_null()) {
        row = json{{"name", entry.name}, {"type", ""},
          {"sources", json::array()}, {"remote", false}};
      }
      if (row["type"].get<std::string>().empty() || entry.source == "local") {
        row["type"] = entry.type;
      }
      row["sources"].push_back(entry.source);
      if (entry.source.rfind("remote:", 0) == 0) {
        row["remote"] = true;
      }
    }
    json out = json::array();
    for (auto & entry : rows) {
      out.push_back(entry.second);
    }
    return out;
  }

  static json nodeList()
  {
    json out = json::array();
    for (const auto & entry : ros2_shim::graph::list_nodes()) {
      out.push_back({{"name", entry.name}, {"source", entry.source}});
    }
    return out;
  }

  json frameList() const
  {
    json out = json::array();
    std::istringstream stream(buffer_->allFramesAsString());
    std::string token;
    while (stream >> token) {
      if (token.rfind("(->", 0) != 0) {
        out.push_back(token);
      }
    }
    return out;
  }

  // ------------------------------------------------------------ fixed frame
  std::string fixedFrame() const
  {
    std::lock_guard<std::mutex> lock(fixed_mutex_);
    return fixed_frame_;
  }

  void setFixedFrame(const std::string & frame)
  {
    if (frame.empty()) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(fixed_mutex_);
      fixed_frame_ = frame;
    }
    std::lock_guard<std::mutex> lock(subs_mutex_);
    for (auto & entry : subs_) {
      if (entry.second->enabled) {
        entry.second->emit();
      }
    }
  }

  Xform lookup(const std::string & source, const builtin_interfaces::msg::Time & stamp)
  {
    const std::string fixed = fixedFrame();
    if (source.empty() || source == fixed) {
      return identity();
    }
    try {
      if (stamp.sec != 0 || stamp.nanosec != 0) {
        return fromMsg(buffer_->lookupTransform(fixed, source, stamp));
      }
      return fromMsg(buffer_->lookupTransform(fixed, source));
    } catch (const tf2::TransformException &) {
      try {
        return fromMsg(buffer_->lookupTransform(fixed, source));
      } catch (const tf2::TransformException &) {
        return Xform{};
      }
    }
  }

  // ---------------------------------------------------------- subscriptions
  void subscribe(const json & command)
  {
    const std::string topic = command.value("topic", "");
    std::string type = command.value("type", "");
    const bool transient = command.value("transient_local", false);
    const double max_hz = command.value("max_hz", 0.0);
    if (topic.empty()) {
      throw std::runtime_error("subscribe needs a topic");
    }
    if (type.empty()) {
      for (const auto & entry : ros2_shim::graph::list_topics()) {
        if (entry.name == topic && !entry.type.empty()) {
          type = entry.type;
          break;
        }
      }
    }
    if (type.empty()) {
      throw std::runtime_error("no type known for '" + topic + "' yet");
    }
    if (std::strlen(kindForType(type)) == 0) {
      if (topic == "/tf" || topic == "/tf_static") {
        return;  // the transform listener already consumes these
      }
      throw std::runtime_error("viz_feed has no decoder for " + type);
    }

    std::lock_guard<std::mutex> lock(subs_mutex_);
    auto existing = subs_.find(topic);
    if (existing != subs_.end()) {
      existing->second->enabled = true;
      existing->second->emit();
      return;
    }
    auto created = createSubscription(topic, type, transient, max_hz);
    if (created) {
      subs_[topic] = std::move(created);
    }
  }

  std::shared_ptr<Subscription> createSubscription(
    const std::string & topic, const std::string & type, bool transient, double max_hz)
  {
    if (type == "nav_msgs/msg/OccupancyGrid") {
      return make<nav_msgs::msg::OccupancyGrid>(topic, type, transient, max_hz);
    }
    if (type == "sensor_msgs/msg/LaserScan") {
      return make<sensor_msgs::msg::LaserScan>(topic, type, transient, max_hz);
    }
    if (type == "nav_msgs/msg/Path") {
      return make<nav_msgs::msg::Path>(topic, type, transient, max_hz);
    }
    if (type == "geometry_msgs/msg/PoseStamped") {
      return make<geometry_msgs::msg::PoseStamped>(topic, type, transient, max_hz);
    }
    if (type == "geometry_msgs/msg/PoseWithCovarianceStamped") {
      return make<geometry_msgs::msg::PoseWithCovarianceStamped>(topic, type, transient, max_hz);
    }
    if (type == "nav_msgs/msg/Odometry") {
      return make<nav_msgs::msg::Odometry>(topic, type, transient, max_hz);
    }
    return nullptr;
  }

  template<class MessageT>
  std::shared_ptr<Subscription> make(
    const std::string & topic, const std::string & type, bool transient, double max_hz)
  {
    const std::string topic_copy = topic;
    auto encode = [this, topic_copy](
      const MessageT & message, const Xform & xform) {
        this->encodeMessage(topic_copy, message, xform);
      };
    auto transform = [this](const MessageT & message) {
        return this->lookup(message.header.frame_id, message.header.stamp);
      };
    auto error_sink = [this](json event) {out_.json(event);};
    return std::make_shared<TypedSubscription<MessageT>>(
      node_, topic, type, transient, max_hz, encode, transform, error_sink);
  }

  void unsubscribe(const std::string & topic)
  {
    std::lock_guard<std::mutex> lock(subs_mutex_);
    auto it = subs_.find(topic);
    if (it != subs_.end()) {
      it->second->enabled = false;
    }
  }

  // --------------------------------------------------------------- encoding
  void emitFrame(const std::string & topic, const char * kind, json data)
  {
    json frame;
    frame["event"] = "frame";
    frame["topic"] = topic;
    frame["kind"] = kind;
    frame["data"] = std::move(data);
    hub_.set(topic, frame.dump());
  }

  void encodeMessage(
    const std::string & topic, const nav_msgs::msg::OccupancyGrid & message,
    const Xform & xform)
  {
    const auto & origin = message.info.origin;
    const Vec3 position = rotate(
      xform.q, Vec3{origin.position.x, origin.position.y, origin.position.z});
    const Quat orientation = mul(
      xform.q, Quat{origin.orientation.x, origin.orientation.y, origin.orientation.z,
        origin.orientation.w});
    json data;
    data["frame"] = fixedFrame();
    data["source_frame"] = message.header.frame_id;
    data["stamp_ns"] = stampNs(message.header.stamp);
    data["width"] = message.info.width;
    data["height"] = message.info.height;
    data["resolution"] = message.info.resolution;
    data["origin"] = json::array(
      {xform.t.x + position.x, xform.t.y + position.y, yawOf(orientation)});
    data["data"] = base64(
      reinterpret_cast<const std::uint8_t *>(message.data.data()), message.data.size());
    emitFrame(topic, "occupancy_grid", std::move(data));
  }

  void encodeMessage(
    const std::string & topic, const sensor_msgs::msg::LaserScan & message,
    const Xform & xform)
  {
    std::vector<double> points;
    points.reserve(message.ranges.size() * 2);
    const double angle_min = message.angle_min;
    const double increment = message.angle_increment;
    for (std::size_t i = 0; i < message.ranges.size(); ++i) {
      const float range = message.ranges[i];
      if (!std::isfinite(range) || range < message.range_min || range > message.range_max) {
        continue;
      }
      const double angle = angle_min + static_cast<double>(i) * increment;
      const Vec3 world = rotate(
        xform.q, Vec3{static_cast<double>(range) * std::cos(angle),
          static_cast<double>(range) * std::sin(angle), 0.0});
      points.push_back(xform.t.x + world.x);
      points.push_back(xform.t.y + world.y);
    }
    json data;
    data["frame"] = fixedFrame();
    data["source_frame"] = message.header.frame_id;
    data["stamp_ns"] = stampNs(message.header.stamp);
    data["range_min"] = message.range_min;
    data["range_max"] = message.range_max;
    data["points"] = points;
    emitFrame(topic, "laser_scan", std::move(data));
  }

  void encodeMessage(
    const std::string & topic, const nav_msgs::msg::Path & message, const Xform & xform)
  {
    std::vector<double> poses;
    poses.reserve(message.poses.size() * 3);
    for (const auto & stamped : message.poses) {
      const auto & pose = stamped.pose;
      const Vec3 position = rotate(
        xform.q, Vec3{pose.position.x, pose.position.y, pose.position.z});
      const Quat orientation = mul(
        xform.q, Quat{pose.orientation.x, pose.orientation.y, pose.orientation.z,
          pose.orientation.w});
      poses.push_back(xform.t.x + position.x);
      poses.push_back(xform.t.y + position.y);
      poses.push_back(yawOf(orientation));
    }
    json data;
    data["frame"] = fixedFrame();
    data["source_frame"] = message.header.frame_id;
    data["stamp_ns"] = stampNs(message.header.stamp);
    data["poses"] = poses;
    emitFrame(topic, "path", std::move(data));
  }

  void encodeMessage(
    const std::string & topic, const geometry_msgs::msg::PoseStamped & message,
    const Xform & xform)
  {
    const auto & pose = message.pose;
    const Vec3 position = rotate(
      xform.q, Vec3{pose.position.x, pose.position.y, pose.position.z});
    const Quat orientation = mul(
      xform.q, Quat{pose.orientation.x, pose.orientation.y, pose.orientation.z,
        pose.orientation.w});
    json data;
    data["frame"] = fixedFrame();
    data["source_frame"] = message.header.frame_id;
    data["stamp_ns"] = stampNs(message.header.stamp);
    data["x"] = xform.t.x + position.x;
    data["y"] = xform.t.y + position.y;
    data["yaw"] = yawOf(orientation);
    emitFrame(topic, "pose", std::move(data));
  }

  void encodeMessage(
    const std::string & topic,
    const geometry_msgs::msg::PoseWithCovarianceStamped & message, const Xform & xform)
  {
    const auto & pose = message.pose.pose;
    const Vec3 position = rotate(
      xform.q, Vec3{pose.position.x, pose.position.y, pose.position.z});
    const Quat orientation = mul(
      xform.q, Quat{pose.orientation.x, pose.orientation.y, pose.orientation.z,
        pose.orientation.w});
    const double c = std::cos(yawOf(xform.q));
    const double s = std::sin(yawOf(xform.q));
    const double xx = message.pose.covariance[0];
    const double xy = message.pose.covariance[1];
    const double yy = message.pose.covariance[7];
    const double rxx = xx * c * c + 2.0 * xy * c * s + yy * s * s;
    const double rxy = (yy - xx) * c * s + xy * (c * c - s * s);
    const double ryy = xx * s * s - 2.0 * xy * c * s + yy * c * c;
    json data;
    data["frame"] = fixedFrame();
    data["source_frame"] = message.header.frame_id;
    data["stamp_ns"] = stampNs(message.header.stamp);
    data["x"] = xform.t.x + position.x;
    data["y"] = xform.t.y + position.y;
    data["yaw"] = yawOf(orientation);
    data["cov"] = json::array({rxx, rxy, ryy});
    data["yaw_var"] = message.pose.covariance[35];
    emitFrame(topic, "pose_covariance", std::move(data));
  }

  void encodeMessage(
    const std::string & topic, const nav_msgs::msg::Odometry & message, const Xform & xform)
  {
    const auto & pose = message.pose.pose;
    const Vec3 position = rotate(
      xform.q, Vec3{pose.position.x, pose.position.y, pose.position.z});
    const Quat orientation = mul(
      xform.q, Quat{pose.orientation.x, pose.orientation.y, pose.orientation.z,
        pose.orientation.w});
    const double c = std::cos(yawOf(xform.q));
    const double s = std::sin(yawOf(xform.q));
    const double xx = message.pose.covariance[0];
    const double xy = message.pose.covariance[1];
    const double yy = message.pose.covariance[7];
    const double rxx = xx * c * c + 2.0 * xy * c * s + yy * s * s;
    const double rxy = (yy - xx) * c * s + xy * (c * c - s * s);
    const double ryy = xx * s * s - 2.0 * xy * c * s + yy * c * c;
    json data;
    data["frame"] = fixedFrame();
    data["source_frame"] = message.header.frame_id;
    data["child_frame"] = message.child_frame_id;
    data["stamp_ns"] = stampNs(message.header.stamp);
    data["x"] = xform.t.x + position.x;
    data["y"] = xform.t.y + position.y;
    data["yaw"] = yawOf(orientation);
    data["vx"] = message.twist.twist.linear.x;
    data["wz"] = message.twist.twist.angular.z;
    data["cov"] = json::array({rxx, rxy, ryy});
    emitFrame(topic, "odometry", std::move(data));
  }

  // ------------------------------------------------------------------ tools
  json publishPose(const json & command)
  {
    const std::string topic = command.value("topic", "/goal_pose");
    const std::string type = command.value("type", "geometry_msgs/msg/PoseStamped");
    const std::string frame = command.value("frame", "map");
    const double x = command.value("x", 0.0);
    const double y = command.value("y", 0.0);
    const double yaw = command.value("yaw", 0.0);
    const int times = std::max(1, command.value("times", 3));
    const Quat orientation = fromYaw(yaw);

    if (type == "geometry_msgs/msg/PoseStamped") {
      geometry_msgs::msg::PoseStamped message;
      message.header.stamp = node_->now().to_message();
      message.header.frame_id = frame;
      message.pose.position.x = x;
      message.pose.position.y = y;
      message.pose.orientation.x = orientation.x;
      message.pose.orientation.y = orientation.y;
      message.pose.orientation.z = orientation.z;
      message.pose.orientation.w = orientation.w;
      auto channel = toolChannel(topic, type);
      const auto payload = ros2_shim::serialize(message);
      for (int i = 0; i < times; ++i) {
        if (!channel->publish(payload.data(), payload.size())) {
          throw std::runtime_error("the goal does not fit the '" + topic + "' slot");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
      }
      return {{"topic", topic}, {"type", type}, {"times", times}};
    }
    if (type == "geometry_msgs/msg/PoseWithCovarianceStamped") {
      geometry_msgs::msg::PoseWithCovarianceStamped message;
      message.header.stamp = node_->now().to_message();
      message.header.frame_id = frame;
      message.pose.pose.position.x = x;
      message.pose.pose.position.y = y;
      message.pose.pose.orientation.x = orientation.x;
      message.pose.pose.orientation.y = orientation.y;
      message.pose.pose.orientation.z = orientation.z;
      message.pose.pose.orientation.w = orientation.w;
      std::fill(message.pose.covariance.begin(), message.pose.covariance.end(), 0.0);
      message.pose.covariance[0] = 0.25;
      message.pose.covariance[7] = 0.25;
      message.pose.covariance[35] = 0.06853891945200942;
      auto channel = toolChannel(topic, type);
      const auto payload = ros2_shim::serialize(message);
      for (int i = 0; i < times; ++i) {
        if (!channel->publish(payload.data(), payload.size())) {
          throw std::runtime_error("the initial pose does not fit the '" + topic + "' slot");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
      }
      return {{"topic", topic}, {"type", type}, {"times", times}};
    }
    throw std::runtime_error("publish_pose only supports PoseStamped and PoseWithCovarianceStamped");
  }

  // ---------------------------------------------------------------- threads
  void spinLoop()
  {
    while (g_running.load()) {
      node_->spin_once();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  void framesLoop()
  {
    std::string previous;
    while (g_running.load()) {
      for (int i = 0; i < 10 && g_running.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      const json frames = frameList();
      const std::string text = frames.dump();
      if (text != previous) {
        previous = text;
        out_.json(json{{"event", "frames"}, {"frames", frames}});
      }
    }
  }

  // One writer per channel, and it is this process. Advertising the topic in
  // the graph is what makes the bridge's uplink and nav2's own nodes see a
  // publisher before the first goal is written.
  std::shared_ptr<shm_transport::ShmChannel> toolChannel(
    const std::string & topic, const std::string & type)
  {
    std::lock_guard<std::mutex> lock(pubs_mutex_);
    auto it = tool_channels_.find(topic);
    if (it != tool_channels_.end()) {
      return it->second;
    }
    std::string error;
    auto channel = ros2_bridge::create_channel(topic, &error);
    if (!channel) {
      throw std::runtime_error("cannot create '" + topic + "': " + error);
    }
    ros2_shim::graph::ProcessRegistry::instance().add_node("/viz_feed");
    ros2_shim::graph::ProcessRegistry::instance().add_topic(topic, type, "pub");
    tool_channels_[topic] = channel;
    return channel;
  }

  Out out_;
  FrameHub hub_;
  rclcpp::Node::SharedPtr node_;
  std::shared_ptr<tf2_ros::Buffer> buffer_;
  std::shared_ptr<tf2_ros::TransformListener> listener_;
  std::mutex pubs_mutex_;
  std::map<std::string, std::shared_ptr<shm_transport::ShmChannel>> tool_channels_;

  mutable std::mutex fixed_mutex_;
  std::string fixed_frame_ = "map";

  std::mutex subs_mutex_;
  std::map<std::string, std::shared_ptr<Subscription>> subs_;

  std::thread spin_thread_;
  std::thread writer_thread_;
  std::thread frames_thread_;
};

// -------------------------------------------------------------- selftest

void runSelfTest()
{
  // No node, no shared memory: exercise the JSON contract with synthetic
  // messages. This is what ctest can assert without a running nav2 stack.
  nav_msgs::msg::OccupancyGrid grid;
  grid.info.width = 4;
  grid.info.height = 2;
  grid.info.resolution = 0.5;
  grid.info.origin.orientation.w = 1.0;
  grid.data = {0, 100, -1, 0, 0, 100, -1, 0};
  json map;
  map["width"] = grid.info.width;
  map["height"] = grid.info.height;
  map["resolution"] = grid.info.resolution;
  map["data"] = base64(
    reinterpret_cast<const std::uint8_t *>(grid.data.data()), grid.data.size());

  sensor_msgs::msg::LaserScan scan;
  scan.angle_min = 0;
  scan.angle_increment = 0.5;
  scan.range_min = 0.1;
  scan.range_max = 10;
  scan.ranges = {1.0f, 2.0f, 3.0f};
  std::vector<double> points;
  for (std::size_t i = 0; i < scan.ranges.size(); ++i) {
    const double angle = scan.angle_min + static_cast<double>(i) * scan.angle_increment;
    points.push_back(scan.ranges[i] * std::cos(angle));
    points.push_back(scan.ranges[i] * std::sin(angle));
  }
  json laser;
  laser["points"] = points;

  const Quat q = mul(fromYaw(1.5707963267948966), fromYaw(0.5));
  json pose;
  pose["x"] = 3.0;
  pose["y"] = 4.0;
  pose["yaw"] = yawOf(q);

  json out;
  out["ok"] = true;
  out["map"] = map;
  out["laser"] = laser;
  out["pose"] = pose;
  std::printf("%s\n", out.dump().c_str());
}

}  // namespace

int main(int argc, char ** argv)
{
  setBinaryStdio();
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  bool self_test = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--selftest") == 0) {
      self_test = true;
    }
  }

  if (self_test) {
    runSelfTest();
    return 0;
  }

  Sidecar sidecar;
  sidecar.start();
  return sidecar.serve();
}