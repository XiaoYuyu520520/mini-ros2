import io

def patch(path, pairs):
    s = io.open(path, encoding="utf-8", newline="").read()
    for old, new in pairs:
        if s.count(old) != 1:
            raise SystemExit("PATCH FAIL (%d) in %s:\n%s" % (s.count(old), path, old[:400]))
        s = s.replace(old, new)
    io.open(path, "w", encoding="utf-8", newline="\n").write(s)
    print("patched", path)

p = "bridge/windows/src/bridge_daemon.cpp"

patch(p, [
# ---------------------------------------------------------------- doc header
("""// * advertises the mirrored topics in the shim graph registry, so
//   `ros2 topic list` shows them,""",
"""// * advertises the mirrored topics in the shim graph registry, so
//   `ros2 topic list` shows them,
// * does the same for services: every remote service the bridge can translate
//   is announced in the shim graph and answered on `<name>/_request`, so a
//   nav2 node's `rclcpp::Client` - or `ros2 service call` - reaches a service
//   running on the robot as if it were served locally,"""),

# ------------------------------------------------------------------- includes
("""#include "ros2_bridge/shm_topic.hpp"
#include "ros2_bridge/type_registry.hpp\"""",
"""#include "ros2_bridge/service_registry.hpp"
#include "ros2_bridge/shm_topic.hpp"
#include "ros2_bridge/type_registry.hpp\""""),

("""using ros2_grpc_sdk::Bytes;
using ros2_grpc_sdk::StatusCode;
using ros2_grpc_sdk::Status;
using ros2_grpc_sdk::TopicInfo;""",
"""using ros2_grpc_sdk::Bytes;
using ros2_grpc_sdk::ServiceInfo;
using ros2_grpc_sdk::StatusCode;
using ros2_grpc_sdk::Status;
using ros2_grpc_sdk::TopicInfo;"""),

# -------------------------------------------------------------------- options
("""  std::vector<std::string> mirror_filter;  // empty = every supported topic
  std::vector<std::string> uplink{"/cmd_vel", "/goal_pose", "/initialpose"};""",
"""  std::vector<std::string> mirror_filter;  // empty = every supported topic
  // Services mirrored from the agent. `--no-services` turns the whole service
  // half off, `--service REGEX` narrows it down the way --mirror does topics.
  bool services = true;
  std::vector<std::string> service_filter;
  std::vector<std::string> uplink{"/cmd_vel", "/goal_pose", "/initialpose"};"""),

("""    "  --mirror REGEX        only mirror matching topics  (repeatable)\\n\"""",
"""    "  --mirror REGEX        only mirror matching topics  (repeatable)\\n"
    "  --service REGEX       only serve matching remote services (repeatable)\\n"
    "  --no-services         do not mirror/serve the agent's services\\n\""""),

("""    } else if (arg == "--uplink") {""",
"""    } else if (arg == "--service") {
      options.service_filter.push_back(next(i));
    } else if (arg == "--no-services") {
      options.services = false;
    } else if (arg == "--uplink") {"""),

# ------------------------------------------------------------- state + struct
("""  struct Uplink
  {""",
"""  // One service the robot serves, answered here by the daemon.
  //
  // The shim carries a service call over two shared-memory channels
  // (`<name>/_request`, `<name>/_response`) that hold a correlation id plus
  // the shim's own layout of the request/response - see
  // ros2_shim/include/rclcpp/service.hpp. A remote service has no process here
  // to write the second channel, so the daemon takes that role: it owns both
  // channels, subscribes to the requests, forwards each one over gRPC and
  // publishes the answer back. From the Windows side the pair is
  // indistinguishable from a service served in this machine's own process.
  struct Service
  {
    std::string name;
    std::string type;
    const ServiceCodec * codec = nullptr;
    std::shared_ptr<shm_transport::ShmChannel> requests;
    std::shared_ptr<shm_transport::ShmChannel> responses;
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> failures{0};
    std::atomic<std::int64_t> last_elapsed_ns{0};
    bool running = false;
    std::thread worker;
  };

  struct Uplink
  {"""),

# --------------------------------------------------------------- run() wiring
("""    if (syncTopics()) {
      signalReady("ready");
    }

    if (options_.once) {
      report();
      return 0;
    }

    startUplinks();""",
"""    if (syncTopics()) {
      syncServices();
      signalReady("ready");
    }

    if (options_.once) {
      report();
      return 0;
    }

    startUplinks();"""),

("""      if (now >= next_refresh) {
        syncTopics();
        startUplinks();""",
"""      if (now >= next_refresh) {
        syncTopics();
        syncServices();
        startUplinks();"""),

# --------------------------------------------------- ready file reports both
("""    std::size_t topics = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      topics = remote_topics_.size();
    }
    std::ofstream out(options_.ready_file, std::ios::trunc);""",
"""    std::size_t topics = 0;
    std::size_t services = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      topics = remote_topics_.size();
      services = services_.size();
    }
    std::ofstream out(options_.ready_file, std::ios::trunc);"""),

("""    out << "status=" << status << " peer=" << peer_ << " topics=" << topics << "\\n";""",
"""    out << "status=" << status << " peer=" << peer_ << " topics=" << topics
        << " services=" << services << "\\n";"""),

# ------------------------------------------------------------ services block
("""  // ---------------------------------------------------------------- uplink

  void startUplinks()""",
"""  // -------------------------------------------------------------- services

  // Mirrors the agent's services: each one gets a graph record and a worker
  // that plays the server role on the shim side. A service this machine
  // already serves is left alone - the shim's channels have exactly one
  // writer, and a local server is authoritative for its own name.
  bool syncServices()
  {
    if (!options_.services) {
      return true;
    }
    std::vector<ServiceInfo> services;
    const Status status = client_.listServices(services);
    if (!status.ok()) {
      std::fprintf(stderr, "[bridge] listServices failed: %s\\n", status.message().c_str());
      return false;
    }

    for (const auto & service : services) {
      if (service.name.empty()) {
        continue;
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (services_.count(service.name) > 0) {
          continue;
        }
      }
      if (local_service_exists(service.name)) {
        continue;
      }
      if (!options_.service_filter.empty() &&
        !matchesAny(service.name, options_.service_filter))
      {
        continue;
      }
      const ServiceCodec * codec = ros2_bridge::find_service_codec(service.type);
      if (codec == nullptr) {
        // Only worth a line once per type: a robot has a handful of types and
        // hundreds of services (every nav2 node has a `~/change_state`).
        if (warned_types_.count(service.type) == 0) {
          warned_types_.insert(service.type);
          std::printf(
            "[bridge] skipping '%s': no codec for service type '%s'\\n",
            service.name.c_str(), service.type.c_str());
        }
        continue;
      }
      startService(service, codec);
    }
    return true;
  }

  // True when a process on this machine serves the name itself. `list_topics`'
  // sibling: the merged view carries the bridge's own records as
  // `remote:<peer>`, so only the local ones answer this.
  static bool local_service_exists(const std::string & name)
  {
    for (const auto & entry : ros2_shim::graph::list_services()) {
      if (entry.name == name && entry.source == "local") {
        return true;
      }
    }
    return false;
  }

  void startService(const ServiceInfo & info, const ServiceCodec * codec)
  {
    auto service = std::make_shared<Service>();
    service->name = info.name;
    service->type = ros2_bridge::canonical_service_type(info.type);
    service->codec = codec;

    std::string error;
    // Created rather than opened, and held for as long as the mirror lasts:
    // both channels are owned by whoever is advertised as the remote end, and
    // a `Local\\` mapping disappears with its last handle. Keeping them here is
    // what lets a client that shows up an hour later still find them.
    service->requests = ros2_bridge::create_channel(service_channel(info.name, "request"), &error);
    if (!service->requests) {
      std::fprintf(
        stderr, "[bridge] cannot create the request channel for '%s': %s\\n",
        info.name.c_str(), error.c_str());
      return;
    }
    service->responses = ros2_bridge::create_channel(service_channel(info.name, "response"), &error);
    if (!service->responses) {
      std::fprintf(
        stderr, "[bridge] cannot create the response channel for '%s': %s\\n",
        info.name.c_str(), error.c_str());
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (services_.count(info.name) > 0) {
        return;
      }
      services_[info.name] = service;
    }

    // Advertised as long as the bridge can answer, which is what makes a
    // client's `wait_for_service()` say yes instead of timing out.
    ros2_shim::graph::ProcessRegistry::instance().add_service(
      info.name, service->type, "remote:" + peer_);

    if (options_.once) {
      return;
    }
    service->running = true;
    service->worker = std::thread([this, service]() { serviceLoop(service); });
    std::printf(
      "[bridge] serving %s [%s] on shm '%s'\\n", info.name.c_str(), service->type.c_str(),
      ros2_bridge::channel_name(service_channel(info.name, "request")).c_str());
  }

  // `<service>/<role>`, the two channel names rclcpp::Node::create_service and
  // create_client agree on.
  static std::string service_channel(const std::string & service, const char * role)
  {
    return service + "/_" + role;
  }

  // The server half of the shim's service protocol: a request arrives with a
  // correlation id in front of it, goes out over gRPC, and the answer goes
  // back on the response channel under the same id.
  //
  // A call that fails has no answer to give - the shim's protocol (like ROS2's
  // own services) carries a response, not a status, and inventing one would
  // hand the caller a default-constructed success. The request is counted and
  // said out loud instead, and the caller times out the way it would for a
  // service that vanished mid-call.
  void serviceLoop(const std::shared_ptr<Service> & service)
  {
    const ServiceCodec & codec = *service->codec;
    std::vector<std::uint8_t> buffer(service->requests->slot_size());
    while (g_running.load() && service->running) {
      std::size_t size = 0;
      if (!service->requests->subscribe(buffer.data(), buffer.size(), &size, 100)) {
        continue;
      }
      if (size < sizeof(std::uint64_t)) {
        continue;
      }
      std::uint64_t correlation = 0;
      std::memcpy(&correlation, buffer.data(), sizeof(correlation));
      const Bytes shim(buffer.begin() + sizeof(correlation), buffer.begin() + size);

      Bytes request_cdr;
      if (!codec.request_to_cdr(shim, request_cdr)) {
        service->failures.fetch_add(1);
        std::fprintf(
          stderr, "[bridge] '%s': undecodable request from shared memory\\n",
          service->name.c_str());
        continue;
      }

      ros2_grpc_sdk::ServiceCallResult result;
      const Status status = client_.callService(
        service->name, service->type, request_cdr, result, "cdr");
      if (!status.ok()) {
        service->failures.fetch_add(1);
        std::fprintf(
          stderr, "[bridge] '%s': call failed: %s\\n", service->name.c_str(),
          status.toString().c_str());
        continue;
      }
      if (!result.success) {
        service->failures.fetch_add(1);
        std::fprintf(
          stderr, "[bridge] '%s': %s\\n", service->name.c_str(), result.error.c_str());
        continue;
      }

      Bytes response_shim;
      if (!codec.response_to_shim(result.response, response_shim)) {
        service->failures.fetch_add(1);
        std::fprintf(
          stderr, "[bridge] '%s': undecodable response for %s\\n", service->name.c_str(),
          service->type.c_str());
        continue;
      }

      Bytes out(sizeof(correlation) + response_shim.size());
      std::memcpy(out.data(), &correlation, sizeof(correlation));
      std::memcpy(out.data() + sizeof(correlation), response_shim.data(), response_shim.size());
      if (!service->responses->publish(out.data(), out.size())) {
        service->failures.fetch_add(1);
        std::fprintf(
          stderr, "[bridge] '%s': the answer does not fit the response slot (%zu bytes)\\n",
          service->name.c_str(), out.size());
        continue;
      }
      service->calls.fetch_add(1);
      service->last_elapsed_ns.store(result.elapsed_ns);
    }
  }

  // ---------------------------------------------------------------- uplink

  void startUplinks()"""),

# ----------------------------------------------------------------- reporting
("""    std::printf("[bridge] peer=%s mirrored=%zu uplink=%zu\\n", peer_.c_str(), mirrors.size(), uplinks.size());""",
"""    std::vector<std::shared_ptr<Service>> services;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (const auto & entry : services_) {
        services.push_back(entry.second);
      }
    }
    std::printf(
      "[bridge] peer=%s mirrored=%zu services=%zu uplink=%zu\\n", peer_.c_str(), mirrors.size(),
      services.size(), uplinks.size());"""),

("""    for (const auto & uplink : uplinks) {
      std::printf(
        "  ^ %-26s %-34s msgs=%-8llu failed=%llu\\n", uplink->topic.c_str(),""",
"""    for (const auto & service : services) {
      std::printf(
        "  s %-26s %-34s calls=%-8llu failed=%llu\\n", service->name.c_str(),
        service->type.c_str(), static_cast<unsigned long long>(service->calls.load()),
        static_cast<unsigned long long>(service->failures.load()));
    }
    for (const auto & uplink : uplinks) {
      std::printf(
        "  ^ %-26s %-34s msgs=%-8llu failed=%llu\\n", uplink->topic.c_str(),"""),

# ---------------------------------------------------------------- shutdown
("""  void shutdown()
  {
    std::vector<std::shared_ptr<Uplink>> uplinks;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (const auto & entry : uplinks_) {
        entry.second->running = false;
        uplinks.push_back(entry.second);
      }
    }""",
"""  void shutdown()
  {
    std::vector<std::shared_ptr<Service>> services;
    std::vector<std::shared_ptr<Uplink>> uplinks;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (const auto & entry : services_) {
        entry.second->running = false;
        services.push_back(entry.second);
      }
      for (const auto & entry : uplinks_) {
        entry.second->running = false;
        uplinks.push_back(entry.second);
      }
    }
    // Stopped before the client is torn down: a worker blocked in a
    // CallService has to be out of the way before the channel goes.
    for (const auto & service : services) {
      if (service->worker.joinable()) {
        service->worker.join();
      }
      ros2_shim::graph::ProcessRegistry::instance().remove_service(
        service->name, "remote:" + peer_);
    }"""),

("""    ros2_shim::graph::ProcessRegistry::instance().clear_topics_from_source("remote:" + peer_);""",
"""    ros2_shim::graph::ProcessRegistry::instance().clear_topics_from_source("remote:" + peer_);
    ros2_shim::graph::ProcessRegistry::instance().clear_services_from_source("remote:" + peer_);"""),

# ------------------------------------------------------------------- members
("""  std::map<std::string, std::shared_ptr<Uplink>> uplinks_;
  std::set<std::string> warned_types_;""",
"""  std::map<std::string, std::shared_ptr<Uplink>> uplinks_;
  // The remote services this daemon answers for, keyed by service name.
  std::map<std::string, std::shared_ptr<Service>> services_;
  std::set<std::string> warned_types_;"""),
])
