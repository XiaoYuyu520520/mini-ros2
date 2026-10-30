// bridge_service_roundtrip - the service half of the bridge, with the network
// taken out.
//
// A service call made by a node on this machine and a service call the bridge
// forwards to the robot are the *same* operation once they reach shared
// memory: a request lands on `<name>/_request` with a correlation id in front
// of it, and the answer comes back on `<name>/_response` under that id. The
// only thing that decides whether the two ends can talk is whether they agree
// on those names, on the slot sizes behind them, and on the payload layout.
//
// So this test runs the whole loop in one process, with a stand-in for the
// daemon on one side and a stand-in for a nav2 node on the other:
//
//   1. `RemoteService` takes the daemon's role - it creates the channels the
//      way bridge/include/ros2_bridge/shm_topic.hpp does, advertises the name
//      in the shim graph as `remote:test`, and answers every request with the
//      same shim -> CDR -> shim pair of conversions the daemon performs
//      around a gRPC call.
//   2. `rclcpp::Node::create_client()` takes the node's role - the test never
//      names a channel itself, so if the shim and the bridge ever disagreed
//      about `<name>/_request` or about the slot size, the request would sit
//      in a different mapping and this test would hang instead of passing.
//
// Every type in the bridge's service registry is exercised, both directions of
// each conversion, and the *fields* are checked rather than just "something
// came back": a swapped field order would survive a byte count check.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ros2_bridge/service_registry.hpp"
#include "ros2_bridge/shm_topic.hpp"
#include "ros2_shim/graph.hpp"
#include "ros2_shim/serialization.hpp"

#include "rclcpp/rclcpp.hpp"

#include "lifecycle_msgs/srv/change_state.hpp"
#include "lifecycle_msgs/srv/get_available_transitions.hpp"
#include "lifecycle_msgs/srv/get_state.hpp"
#include "nav2_msgs/srv/add_exclusion_zone.hpp"
#include "nav2_msgs/srv/add_shapes.hpp"
#include "nav2_msgs/srv/clear_costmap_around_pose.hpp"
#include "nav2_msgs/srv/clear_costmap_around_robot.hpp"
#include "nav2_msgs/srv/clear_costmap_except_region.hpp"
#include "nav2_msgs/srv/clear_entire_costmap.hpp"
#include "nav2_msgs/srv/dynamic_edges.hpp"
#include "nav2_msgs/srv/get_costmap.hpp"
#include "nav2_msgs/srv/get_shapes.hpp"
#include "nav2_msgs/srv/get_costs.hpp"
#include "nav2_msgs/srv/is_path_valid.hpp"
#include "nav2_msgs/srv/load_map.hpp"
#include "nav2_msgs/srv/manage_lifecycle_nodes.hpp"
#include "nav2_msgs/srv/reload_dock_database.hpp"
#include "nav2_msgs/srv/remove_exclusion_zone.hpp"
#include "nav2_msgs/srv/remove_shapes.hpp"
#include "nav2_msgs/srv/save_map.hpp"
#include "nav2_msgs/srv/set_initial_pose.hpp"
#include "nav2_msgs/srv/set_route_graph.hpp"
#include "nav2_msgs/srv/toggle.hpp"
#include "nav_msgs/srv/get_map.hpp"
#include "slam_toolbox/srv/add_submap.hpp"
#include "slam_toolbox/srv/clear.hpp"
#include "slam_toolbox/srv/clear_queue.hpp"
#include "slam_toolbox/srv/deserialize_pose_graph.hpp"
#include "slam_toolbox/srv/loop_closure.hpp"
#include "slam_toolbox/srv/merge_maps.hpp"
#include "slam_toolbox/srv/pause.hpp"
#include "slam_toolbox/srv/reset.hpp"
#include "slam_toolbox/srv/save_map.hpp"
#include "slam_toolbox/srv/serialize_pose_graph.hpp"
#include "slam_toolbox/srv/toggle_interactive.hpp"
#include "std_srvs/srv/empty.hpp"
#include "std_srvs/srv/set_bool.hpp"
#include "std_srvs/srv/trigger.hpp"

namespace
{

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string & what)
{
  ++g_checks;
  if (ok) {
    std::printf("ok   %s\n", what.c_str());
  } else {
    ++g_failures;
    std::printf("FAIL %s\n", what.c_str());
  }
}

// The daemon's half of a remote service, minus gRPC.
template<class ServiceT>
class RemoteService
{
public:
  using Request = typename ServiceT::Request;
  using Response = typename ServiceT::Response;

  RemoteService(
    const std::string & name, const std::string & type,
    std::function<void(const Request &)> expect, std::function<void(Response &)> respond)
  : name_(name), type_(type), expect_(std::move(expect)), respond_(std::move(respond))
  {
    codec_ = ros2_bridge::find_service_codec(type);
    if (codec_ == nullptr) {
      std::printf("FAIL no codec for %s\n", type.c_str());
      ++g_failures;
      return;
    }
    std::string error;
    requests_ = ros2_bridge::create_service_channel(name, "request", &error);
    responses_ = ros2_bridge::create_service_channel(name, "response", &error);
    if (!requests_ || !responses_) {
      std::printf("FAIL cannot create the channels for %s: %s\n", name.c_str(), error.c_str());
      ++g_failures;
      return;
    }
    ros2_shim::graph::ProcessRegistry::instance().add_service(name, type, "remote:test");
    running_ = true;
    worker_ = std::thread([this]() {loop();});
  }

  ~RemoteService() {stop();}

  void stop()
  {
    if (!running_.exchange(false)) {
      return;
    }
    if (worker_.joinable()) {
      worker_.join();
    }
    ros2_shim::graph::ProcessRegistry::instance().remove_service(name_, "remote:test");
  }

  int served() const {return served_.load();}
  int failures() const {return failures_.load();}

private:
  void loop()
  {
    std::vector<std::uint8_t> buffer(requests_->slot_size());
    while (running_.load()) {
      std::size_t size = 0;
      if (!requests_->subscribe(buffer.data(), buffer.size(), &size, 20)) {
        continue;
      }
      if (size < sizeof(std::uint64_t)) {
        continue;
      }
      std::uint64_t correlation = 0;
      std::memcpy(&correlation, buffer.data(), sizeof(correlation));
      const ros2_bridge::Bytes shim(
        buffer.begin() + sizeof(correlation), buffer.begin() + size);

      // What the daemon does on the way out: shim layout -> CDR.
      ros2_bridge::Bytes cdr;
      if (!codec_->request_to_cdr(shim, cdr)) {
        failures_.fetch_add(1);
        std::printf("FAIL %s: the request is not a valid shim payload\n", name_.c_str());
        continue;
      }
      // ... and what the *service* on the far end does with it: back to a
      // struct, which is how the request's fields get checked at all.
      ros2_bridge::Bytes round_tripped;
      if (!codec_->request_to_shim(cdr, round_tripped)) {
        failures_.fetch_add(1);
        std::printf("FAIL %s: the request does not survive CDR\n", name_.c_str());
        continue;
      }
      Request request;
      if (!ros2_shim::deserialize(round_tripped, request)) {
        failures_.fetch_add(1);
        std::printf("FAIL %s: the round tripped request does not decode\n", name_.c_str());
        continue;
      }
      expect_(request);

      Response response{};
      respond_(response);
      // What the daemon does on the way back: the remote answer as CDR, then
      // into the shim layout the caller reads.
      ros2_bridge::Bytes answer_cdr;
      if (!codec_->response_to_cdr(ros2_shim::serialize(response), answer_cdr)) {
        failures_.fetch_add(1);
        std::printf("FAIL %s: the response does not encode to CDR\n", name_.c_str());
        continue;
      }
      ros2_bridge::Bytes answer_shim;
      if (!codec_->response_to_shim(answer_cdr, answer_shim)) {
        failures_.fetch_add(1);
        std::printf("FAIL %s: the response does not come back out of CDR\n", name_.c_str());
        continue;
      }

      ros2_bridge::Bytes out(sizeof(correlation) + answer_shim.size());
      std::memcpy(out.data(), &correlation, sizeof(correlation));
      std::memcpy(out.data() + sizeof(correlation), answer_shim.data(), answer_shim.size());
      if (!responses_->publish(out.data(), out.size())) {
        failures_.fetch_add(1);
        std::printf("FAIL %s: the answer does not fit the response slot\n", name_.c_str());
        continue;
      }
      served_.fetch_add(1);
    }
  }

  std::string name_;
  std::string type_;
  std::function<void(const Request &)> expect_;
  std::function<void(Response &)> respond_;
  const ros2_bridge::ServiceCodec * codec_ = nullptr;
  std::shared_ptr<shm_transport::ShmChannel> requests_;
  std::shared_ptr<shm_transport::ShmChannel> responses_;
  std::atomic<bool> running_{false};
  std::atomic<int> served_{0};
  std::atomic<int> failures_{0};
  std::thread worker_;
};

// One call end to end: a node's client asks, the stand-in daemon answers.
template<class ServiceT>
void roundTrip(
  const std::string & name, const std::string & type,
  const std::function<void(typename ServiceT::Request &)> & fill,
  const std::function<void(const typename ServiceT::Request &)> & expect,
  const std::function<void(typename ServiceT::Response &)> & respond,
  const std::function<void(const typename ServiceT::Response &)> & verify)
{
  RemoteService<ServiceT> remote(
    name, type, expect,
    [&respond](typename ServiceT::Response & response) {respond(response);});

  auto node = std::make_shared<rclcpp::Node>("service_roundtrip");
  auto client = node->create_client<ServiceT>(name);
  check(client->wait_for_service(std::chrono::seconds(2)), name + ": wait_for_service()");
  check(client->service_is_ready(), name + ": service_is_ready()");

  auto request = std::make_shared<typename ServiceT::Request>();
  fill(*request);
  auto future = client->async_send_request(request);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
    std::chrono::steady_clock::now() < deadline)
  {
    node->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  const bool answered = future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
  check(answered, name + ": the answer came back");
  if (answered) {
    verify(*future.get());
  }
  check(remote.failures() == 0, name + ": every payload converted");
  check(remote.served() == 1, name + ": exactly one call was served");
  remote.stop();
}

// Shorthand for the common case: nothing to look at in the request.
template<class ServiceT>
std::function<void(const typename ServiceT::Request &)> ignoreRequest()
{
  return [](const typename ServiceT::Request &) {};
}

// Shorthand for the other common case: a service that carries no field in
// either direction, which five of the slam_toolbox types are. The names still
// have to match, the two payloads still have to round trip, and the caller
// still has to get an answer - that is the whole of what these types promise.
template<class ServiceT>
void roundTripEmpty(const std::string & name, const std::string & type)
{
  roundTrip<ServiceT>(
    name, type,
    [](typename ServiceT::Request &) {},
    ignoreRequest<ServiceT>(),
    [](typename ServiceT::Response &) {},
    [type](const typename ServiceT::Response &) {
      check(true, type + ": the empty response arrived");
    });
}

}  // namespace

int main()
{
  const std::function<void(std_srvs::srv::Trigger::Response &)> trigger_ok =
    [](std_srvs::srv::Trigger::Response & response) {
      response.success = true;
      response.message = "bridge roundtrip";
    };

  roundTrip<std_srvs::srv::Trigger>(
    "/test/trigger", "std_srvs/srv/Trigger",
    [](std_srvs::srv::Trigger::Request &) {},
    ignoreRequest<std_srvs::srv::Trigger>(),
    trigger_ok,
    [](const std_srvs::srv::Trigger::Response & response) {
      check(response.success, "std_srvs/srv/Trigger: success is true");
      check(response.message == "bridge roundtrip", "std_srvs/srv/Trigger: message text");
    });

  roundTrip<std_srvs::srv::SetBool>(
    "/test/set_bool", "std_srvs/srv/SetBool",
    [](std_srvs::srv::SetBool::Request & request) {request.data = true;},
    [](const std_srvs::srv::SetBool::Request & request) {
      check(request.data, "std_srvs/srv/SetBool: the request's bool arrived");
    },
    [](std_srvs::srv::SetBool::Response & response) {
      response.success = true;
      response.message = "paused";
    },
    [](const std_srvs::srv::SetBool::Response & response) {
      check(response.success, "std_srvs/srv/SetBool: success is true");
      check(response.message == "paused", "std_srvs/srv/SetBool: message text");
    });

  // A request *and* a response with no fields at all: their CDR is the 4 byte
  // encapsulation header plus the 4 byte body a real ROS2 peer needs before it
  // will read the payload back at all (see `shimToCdr`), and a conversion that
  // treated "no bytes" as an error would break a service nav2 relies on.
  roundTrip<std_srvs::srv::Empty>(
    "/test/empty", "std_srvs/srv/Empty",
    [](std_srvs::srv::Empty::Request &) {},
    ignoreRequest<std_srvs::srv::Empty>(),
    [](std_srvs::srv::Empty::Response &) {},
    [](const std_srvs::srv::Empty::Response &) {
      check(true, "std_srvs/srv/Empty: answered");
    });

  // The encoder on its own, because `roundTrip` reads the CDR back with this
  // project's own decoder - which is happy with the body-less payload that Fast
  // CDR, and so a real ROS2 service, refuses.
  if (const ros2_bridge::ServiceCodec * empty_codec =
    ros2_bridge::find_service_codec("std_srvs/srv/Empty"))
  {
    const ros2_bridge::Bytes empty_shim = ros2_shim::serialize(std_srvs::srv::Empty::Request{});
    check(empty_shim.empty(), "std_srvs/srv/Empty: a field-less request is zero shim bytes");
    ros2_bridge::Bytes empty_cdr;
    check(
      empty_codec->request_to_cdr(empty_shim, empty_cdr),
      "std_srvs/srv/Empty: a field-less request still encodes");
    check(
      empty_cdr == ros2_bridge::Bytes{0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      "std_srvs/srv/Empty: the encoder pads the empty body the way rmw_serialize does");
  } else {
    check(false, "std_srvs/srv/Empty: the bridge has a codec for it");
  }

  roundTrip<lifecycle_msgs::srv::GetState>(
    "/test/get_state", "lifecycle_msgs/srv/GetState",
    [](lifecycle_msgs::srv::GetState::Request &) {},
    ignoreRequest<lifecycle_msgs::srv::GetState>(),
    [](lifecycle_msgs::srv::GetState::Response & response) {
      response.current_state.id = lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE;
      response.current_state.label = "active";
    },
    [](const lifecycle_msgs::srv::GetState::Response & response) {
      check(
        response.current_state.id == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE,
        "lifecycle_msgs/srv/GetState: state id");
      check(response.current_state.label == "active", "lifecycle_msgs/srv/GetState: state label");
    });

  roundTrip<lifecycle_msgs::srv::ChangeState>(
    "/test/change_state", "lifecycle_msgs/srv/ChangeState",
    [](lifecycle_msgs::srv::ChangeState::Request & request) {
      request.transition.id = lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE;
      request.transition.label = "activate";
    },
    [](const lifecycle_msgs::srv::ChangeState::Request & request) {
      check(
        request.transition.id == lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE,
        "lifecycle_msgs/srv/ChangeState: transition id arrived");
      check(
        request.transition.label == "activate",
        "lifecycle_msgs/srv/ChangeState: transition label arrived");
    },
    [](lifecycle_msgs::srv::ChangeState::Response & response) {response.success = true;},
    [](const lifecycle_msgs::srv::ChangeState::Response & response) {
      check(response.success, "lifecycle_msgs/srv/ChangeState: success is true");
    });

  roundTrip<lifecycle_msgs::srv::GetAvailableTransitions>(
    "/test/get_available_transitions", "lifecycle_msgs/srv/GetAvailableTransitions",
    [](lifecycle_msgs::srv::GetAvailableTransitions::Request &) {},
    ignoreRequest<lifecycle_msgs::srv::GetAvailableTransitions>(),
    [](lifecycle_msgs::srv::GetAvailableTransitions::Response & response) {
      lifecycle_msgs::msg::TransitionDescription description;
      description.transition.id = lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE;
      description.transition.label = "activate";
      description.start_state.id = lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE;
      description.start_state.label = "inactive";
      description.goal_state.id = lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE;
      description.goal_state.label = "active";
      response.available_transitions.push_back(description);
    },
    [](const lifecycle_msgs::srv::GetAvailableTransitions::Response & response) {
      check(
        response.available_transitions.size() == 1,
        "lifecycle_msgs/srv/GetAvailableTransitions: one transition");
      if (!response.available_transitions.empty()) {
        const auto & entry = response.available_transitions.front();
        check(
          entry.transition.id == lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE,
          "lifecycle_msgs/srv/GetAvailableTransitions: transition survived");
        check(
          entry.goal_state.label == "active",
          "lifecycle_msgs/srv/GetAvailableTransitions: nested state survived");
      }
    });

  roundTrip<nav2_msgs::srv::ManageLifecycleNodes>(
    "/test/manage_lifecycle_nodes", "nav2_msgs/srv/ManageLifecycleNodes",
    [](nav2_msgs::srv::ManageLifecycleNodes::Request & request) {
      request.command = nav2_msgs::srv::ManageLifecycleNodes::Request::STARTUP;
    },
    [](const nav2_msgs::srv::ManageLifecycleNodes::Request & request) {
      check(
        request.command == nav2_msgs::srv::ManageLifecycleNodes::Request::STARTUP,
        "nav2_msgs/srv/ManageLifecycleNodes: command arrived");
    },
    [](nav2_msgs::srv::ManageLifecycleNodes::Response & response) {response.success = true;},
    [](const nav2_msgs::srv::ManageLifecycleNodes::Response & response) {
      check(response.success, "nav2_msgs/srv/ManageLifecycleNodes: success is true");
    });

  // The two nav2 services a robot serves. `SetInitialPose` is the deepest
  // request the bridge carries - a `PoseWithCovarianceStamped` under a service
  // request, i.e. a pose, a covariance array and a header - and `Toggle` is the
  // one whose answer carries a string.
  roundTrip<nav2_msgs::srv::SetInitialPose>(
    "/test/set_initial_pose", "nav2_msgs/srv/SetInitialPose",
    [](nav2_msgs::srv::SetInitialPose::Request & request) {
      request.pose.header.frame_id = "map";
      request.pose.header.stamp.sec = 42;
      request.pose.pose.pose.position.x = 2.0;
      request.pose.pose.pose.position.y = -1.0;
      request.pose.pose.pose.orientation.w = 1.0;
      request.pose.pose.covariance[0] = 0.25;
      request.pose.pose.covariance[35] = 0.5;
    },
    [](const nav2_msgs::srv::SetInitialPose::Request & request) {
      check(request.pose.header.frame_id == "map", "nav2_msgs/srv/SetInitialPose: frame arrived");
      check(request.pose.header.stamp.sec == 42, "nav2_msgs/srv/SetInitialPose: stamp arrived");
      check(
        request.pose.pose.pose.position.x == 2.0 &&
        request.pose.pose.pose.position.y == -1.0,
        "nav2_msgs/srv/SetInitialPose: the nested pose arrived");
      check(
        request.pose.pose.covariance[0] == 0.25 && request.pose.pose.covariance[35] == 0.5,
        "nav2_msgs/srv/SetInitialPose: the fixed size covariance arrived");
    },
    [](nav2_msgs::srv::SetInitialPose::Response &) {},
    [](const nav2_msgs::srv::SetInitialPose::Response &) {
      // The response has no fields, and it still has to come back as a
      // payload: an empty answer is what "no news is good news" looks like.
      check(true, "nav2_msgs/srv/SetInitialPose: the empty response arrived");
    });

  roundTrip<nav2_msgs::srv::Toggle>(
    "/test/toggle", "nav2_msgs/srv/Toggle",
    [](nav2_msgs::srv::Toggle::Request & request) {request.enable = true;},
    [](const nav2_msgs::srv::Toggle::Request & request) {
      check(request.enable, "nav2_msgs/srv/Toggle: the request's bool arrived");
    },
    [](nav2_msgs::srv::Toggle::Response & response) {
      response.success = true;
      response.message = "driving enabled";
    },
    [](const nav2_msgs::srv::Toggle::Response & response) {
      check(response.success, "nav2_msgs/srv/Toggle: success is true");
      check(response.message == "driving enabled", "nav2_msgs/srv/Toggle: message text");
    });

  // The two slam_toolbox services the ported node serves itself. They are the
  // first entries in the table whose request carries a *string*, which is what
  // `ros2 service call slam_toolbox/serialize_map slam_toolbox/srv/
  // SerializePoseGraph filename="..."` sends, and `DeserializePoseGraph` is
  // the one whose request mixes a string, a bounded integer and a nested
  // `Pose2D`.
  roundTrip<slam_toolbox::srv::SerializePoseGraph>(
    "/test/serialize_map", "slam_toolbox/srv/SerializePoseGraph",
    [](slam_toolbox::srv::SerializePoseGraph::Request & request) {
      request.filename = "C:/maps/room.posegraph";
    },
    [](const slam_toolbox::srv::SerializePoseGraph::Request & request) {
      check(
        request.filename == "C:/maps/room.posegraph",
        "slam_toolbox/srv/SerializePoseGraph: the filename arrived");
    },
    [](slam_toolbox::srv::SerializePoseGraph::Response & response) {
      response.result =
        slam_toolbox::srv::SerializePoseGraph::Response::RESULT_SUCCESS;
    },
    [](const slam_toolbox::srv::SerializePoseGraph::Response & response) {
      check(
        response.result ==
        slam_toolbox::srv::SerializePoseGraph::Response::RESULT_SUCCESS,
        "slam_toolbox/srv/SerializePoseGraph: result is RESULT_SUCCESS");
    });

  roundTrip<slam_toolbox::srv::DeserializePoseGraph>(
    "/test/deserialize_map", "slam_toolbox/srv/DeserializePoseGraph",
    [](slam_toolbox::srv::DeserializePoseGraph::Request & request) {
      request.filename = "C:/maps/room";
      request.match_type =
        slam_toolbox::srv::DeserializePoseGraph::Request::START_AT_GIVEN_POSE;
      request.initial_pose.x = 1.25;
      request.initial_pose.y = -3.5;
      request.initial_pose.theta = 0.75;
    },
    [](const slam_toolbox::srv::DeserializePoseGraph::Request & request) {
      check(
        request.filename == "C:/maps/room",
        "slam_toolbox/srv/DeserializePoseGraph: the filename arrived");
      check(
        request.match_type ==
        slam_toolbox::srv::DeserializePoseGraph::Request::START_AT_GIVEN_POSE,
        "slam_toolbox/srv/DeserializePoseGraph: match_type survived as int8");
      check(
        request.initial_pose.x == 1.25 && request.initial_pose.y == -3.5 &&
        request.initial_pose.theta == 0.75,
        "slam_toolbox/srv/DeserializePoseGraph: the nested Pose2D arrived");
    },
    [](slam_toolbox::srv::DeserializePoseGraph::Response &) {},
    [](const slam_toolbox::srv::DeserializePoseGraph::Response &) {
      check(
        true,
        "slam_toolbox/srv/DeserializePoseGraph: the empty response arrived");
    });

  // The rest of the ported node's service surface, which is the same shape as
  // the two above: created on this machine by slam_toolbox, reached over the
  // shared-memory channel. Five of them carry no field in either direction, so
  // they share one body; the other five each carry something worth checking.
  roundTripEmpty<slam_toolbox::srv::Clear>("/test/clear_changes", "slam_toolbox/srv/Clear");
  roundTripEmpty<slam_toolbox::srv::LoopClosure>(
    "/test/manual_loop_closure", "slam_toolbox/srv/LoopClosure");
  roundTripEmpty<slam_toolbox::srv::MergeMaps>(
    "/test/merge_submaps", "slam_toolbox/srv/MergeMaps");
  roundTripEmpty<slam_toolbox::srv::ToggleInteractive>(
    "/test/toggle_interactive", "slam_toolbox/srv/ToggleInteractive");

  // `bool status` on the way back and nothing on the way in, for the two
  // services that acknowledge a queue operation.
  roundTrip<slam_toolbox::srv::Pause>(
    "/test/pause_new_measurements", "slam_toolbox/srv/Pause",
    [](slam_toolbox::srv::Pause::Request &) {},
    ignoreRequest<slam_toolbox::srv::Pause>(),
    [](slam_toolbox::srv::Pause::Response & response) {response.status = true;},
    [](const slam_toolbox::srv::Pause::Response & response) {
      check(response.status, "slam_toolbox/srv/Pause: status came back true");
    });

  roundTrip<slam_toolbox::srv::ClearQueue>(
    "/test/clear_queue", "slam_toolbox/srv/ClearQueue",
    [](slam_toolbox::srv::ClearQueue::Request &) {},
    ignoreRequest<slam_toolbox::srv::ClearQueue>(),
    [](slam_toolbox::srv::ClearQueue::Response & response) {response.status = true;},
    [](const slam_toolbox::srv::ClearQueue::Response & response) {
      check(response.status, "slam_toolbox/srv/ClearQueue: status came back true");
    });

  // A filename in, nothing out: `add_submap` reads a sub-map off disk the way
  // `serialize_map` writes one.
  roundTrip<slam_toolbox::srv::AddSubmap>(
    "/test/add_submap", "slam_toolbox/srv/AddSubmap",
    [](slam_toolbox::srv::AddSubmap::Request & request) {
      request.filename = "C:/maps/submap_1";
    },
    [](const slam_toolbox::srv::AddSubmap::Request & request) {
      check(
        request.filename == "C:/maps/submap_1",
        "slam_toolbox/srv/AddSubmap: the filename arrived");
    },
    [](slam_toolbox::srv::AddSubmap::Response &) {},
    [](const slam_toolbox::srv::AddSubmap::Response &) {
      check(true, "slam_toolbox/srv/AddSubmap: the empty response arrived");
    });

  // A nested `std_msgs/String` in, a `uint8` out. This is the only request in
  // the table whose one field is inside another message, which is why its CLI
  // path is `name.data=...`.
  roundTrip<slam_toolbox::srv::SaveMap>(
    "/test/save_map", "slam_toolbox/srv/SaveMap",
    [](slam_toolbox::srv::SaveMap::Request & request) {
      request.name.data = "C:/maps/room";
    },
    [](const slam_toolbox::srv::SaveMap::Request & request) {
      check(
        request.name.data == "C:/maps/room",
        "slam_toolbox/srv/SaveMap: the nested std_msgs/String arrived");
    },
    [](slam_toolbox::srv::SaveMap::Response & response) {
      response.result = slam_toolbox::srv::SaveMap::Response::RESULT_SUCCESS;
    },
    [](const slam_toolbox::srv::SaveMap::Response & response) {
      check(
        response.result == slam_toolbox::srv::SaveMap::Response::RESULT_SUCCESS,
        "slam_toolbox/srv/SaveMap: result is RESULT_SUCCESS");
    });

  // A flag in, a result code out.
  roundTrip<slam_toolbox::srv::Reset>(
    "/test/reset", "slam_toolbox/srv/Reset",
    [](slam_toolbox::srv::Reset::Request & request) {
      request.pause_new_measurements = true;
    },
    [](const slam_toolbox::srv::Reset::Request & request) {
      check(
        request.pause_new_measurements,
        "slam_toolbox/srv/Reset: pause_new_measurements arrived");
    },
    [](slam_toolbox::srv::Reset::Response & response) {
      response.result = slam_toolbox::srv::Reset::Response::RESULT_SUCCESS;
    },
    [](const slam_toolbox::srv::Reset::Response & response) {
      check(
        response.result == slam_toolbox::srv::Reset::Response::RESULT_SUCCESS,
        "slam_toolbox/srv/Reset: result is RESULT_SUCCESS");
    });

  // `slam_toolbox/dynamic_map` answers with a whole occupancy grid, so this is
  // the only service payload in the table that carries a message with
  // sequences inside a nested message.
  roundTrip<nav_msgs::srv::GetMap>(
    "/test/dynamic_map", "nav_msgs/srv/GetMap",
    [](nav_msgs::srv::GetMap::Request &) {},
    ignoreRequest<nav_msgs::srv::GetMap>(),
    [](nav_msgs::srv::GetMap::Response & response) {
      response.map.header.frame_id = "map";
      response.map.info.resolution = 0.05f;
      response.map.info.width = 2;
      response.map.info.height = 2;
      response.map.info.origin.position.x = -1.0;
      response.map.data = {0, 100, -1, 100};
    },
    [](const nav_msgs::srv::GetMap::Response & response) {
      check(
        response.map.header.frame_id == "map" && response.map.info.width == 2 &&
        response.map.info.height == 2,
        "nav_msgs/srv/GetMap: the grid header and size arrived");
      check(
        response.map.data.size() == 4 && response.map.data[1] == 100 &&
        response.map.data[2] == -1,
        "nav_msgs/srv/GetMap: the occupied and unknown cells arrived");
      check(
        response.map.info.resolution == 0.05f &&
        response.map.info.origin.position.x == -1.0,
        "nav_msgs/srv/GetMap: the resolution and origin arrived");
    });

  // ---------------------------------------------------------------------
  // nav2's own servers. Every one of these is created on this machine by the
  // ported stack - the four clear services by each costmap
  // (nav2_costmap_2d/src/clear_costmap_service.cpp:171-244), `GetCostmap` and
  // `GetCosts` by the costmap, `IsPathValid` by the planner, and the map
  // server's pair - so the call goes through the shared-memory channel.
  roundTrip<nav2_msgs::srv::ClearEntireCostmap>(
    "/test/clear_entirely_global_costmap", "nav2_msgs/srv/ClearEntireCostmap",
    [](nav2_msgs::srv::ClearEntireCostmap::Request & request) {
      request.plugins = {"obstacle_layer", "inflation_layer"};
    },
    [](const nav2_msgs::srv::ClearEntireCostmap::Request & request) {
      check(
        request.plugins.size() == 2 && request.plugins[0] == "obstacle_layer" &&
        request.plugins[1] == "inflation_layer",
        "nav2_msgs/srv/ClearEntireCostmap: the plugins list arrived");
    },
    [](nav2_msgs::srv::ClearEntireCostmap::Response & response) {
      response.success = true;
    },
    [](const nav2_msgs::srv::ClearEntireCostmap::Response & response) {
      check(response.success, "nav2_msgs/srv/ClearEntireCostmap: success came back true");
    });

  roundTrip<nav2_msgs::srv::ClearCostmapAroundRobot>(
    "/test/clear_around_robot", "nav2_msgs/srv/ClearCostmapAroundRobot",
    [](nav2_msgs::srv::ClearCostmapAroundRobot::Request & request) {
      request.reset_distance = 3.5f;
      request.plugins = {"voxel_layer"};
    },
    [](const nav2_msgs::srv::ClearCostmapAroundRobot::Request & request) {
      check(
        request.reset_distance == 3.5f && request.plugins.size() == 1,
        "nav2_msgs/srv/ClearCostmapAroundRobot: the distance and plugin arrived");
    },
    [](nav2_msgs::srv::ClearCostmapAroundRobot::Response & response) {
      response.success = false;
    },
    [](const nav2_msgs::srv::ClearCostmapAroundRobot::Response & response) {
      // False is a real answer here, and it has to survive as one.
      check(!response.success, "nav2_msgs/srv/ClearCostmapAroundRobot: success came back false");
    });

  roundTrip<nav2_msgs::srv::ClearCostmapExceptRegion>(
    "/test/clear_except_region", "nav2_msgs/srv/ClearCostmapExceptRegion",
    [](nav2_msgs::srv::ClearCostmapExceptRegion::Request & request) {
      request.reset_distance = 1.25f;
    },
    [](const nav2_msgs::srv::ClearCostmapExceptRegion::Request & request) {
      check(
        request.reset_distance == 1.25f && request.plugins.empty(),
        "nav2_msgs/srv/ClearCostmapExceptRegion: an empty plugins list stayed empty");
    },
    [](nav2_msgs::srv::ClearCostmapExceptRegion::Response & response) {
      response.success = true;
    },
    [](const nav2_msgs::srv::ClearCostmapExceptRegion::Response & response) {
      check(response.success, "nav2_msgs/srv/ClearCostmapExceptRegion: success came back true");
    });

  roundTrip<nav2_msgs::srv::ClearCostmapAroundPose>(
    "/test/clear_around_pose", "nav2_msgs/srv/ClearCostmapAroundPose",
    [](nav2_msgs::srv::ClearCostmapAroundPose::Request & request) {
      request.pose.header.frame_id = "map";
      request.pose.pose.position.x = 4.5;
      request.pose.pose.position.y = -2.25;
      request.pose.pose.orientation.w = 1.0;
      request.reset_distance = 0.75;
    },
    [](const nav2_msgs::srv::ClearCostmapAroundPose::Request & request) {
      check(
        request.pose.header.frame_id == "map" &&
        request.pose.pose.position.x == 4.5 &&
        request.pose.pose.position.y == -2.25 &&
        request.pose.pose.orientation.w == 1.0,
        "nav2_msgs/srv/ClearCostmapAroundPose: the nested PoseStamped arrived");
      check(
        request.reset_distance == 0.75,
        "nav2_msgs/srv/ClearCostmapAroundPose: reset_distance survived as float64");
    },
    [](nav2_msgs::srv::ClearCostmapAroundPose::Response & response) {
      response.success = true;
    },
    [](const nav2_msgs::srv::ClearCostmapAroundPose::Response & response) {
      check(response.success, "nav2_msgs/srv/ClearCostmapAroundPose: success came back true");
    });

  // The one answer in this table that carries a nested message with a `uint8[]`
  // inside it: a whole costmap, layer name and all.
  roundTrip<nav2_msgs::srv::GetCostmap>(
    "/test/get_costmap", "nav2_msgs/srv/GetCostmap",
    [](nav2_msgs::srv::GetCostmap::Request &) {},
    ignoreRequest<nav2_msgs::srv::GetCostmap>(),
    [](nav2_msgs::srv::GetCostmap::Response & response) {
      response.map.header.frame_id = "map";
      response.map.metadata.layer = "master";
      response.map.metadata.resolution = 0.05f;
      response.map.metadata.size_x = 4;
      response.map.metadata.size_y = 3;
      response.map.metadata.origin.position.x = -1.0;
      response.map.data = {0, 1, 254, 255};
    },
    [](const nav2_msgs::srv::GetCostmap::Response & response) {
      check(
        response.map.metadata.layer == "master" &&
        response.map.metadata.size_x == 4 && response.map.metadata.size_y == 3,
        "nav2_msgs/srv/GetCostmap: the nested Costmap's layer and size arrived");
      check(
        response.map.data.size() == 4 && response.map.data[3] == 255,
        "nav2_msgs/srv/GetCostmap: the uint8 costs arrived, lethal included");
      check(
        response.map.metadata.origin.position.x == -1.0,
        "nav2_msgs/srv/GetCostmap: the origin arrived");
    });

  roundTrip<nav2_msgs::srv::GetCosts>(
    "/test/get_costs", "nav2_msgs/srv/GetCosts",
    [](nav2_msgs::srv::GetCosts::Request & request) {
      request.use_footprint = true;
    },
    [](const nav2_msgs::srv::GetCosts::Request & request) {
      check(
        request.use_footprint && request.poses.empty(),
        "nav2_msgs/srv/GetCosts: use_footprint arrived and poses stayed empty");
    },
    [](nav2_msgs::srv::GetCosts::Response & response) {
      response.costs = {0.0f, 128.0f, 254.0f};
      response.success = true;
    },
    [](const nav2_msgs::srv::GetCosts::Response & response) {
      check(
        response.costs.size() == 3 && response.costs[2] == 254.0f && response.success,
        "nav2_msgs/srv/GetCosts: the float32 costs arrived");
    });

  roundTrip<nav2_msgs::srv::IsPathValid>(
    "/test/is_path_valid", "nav2_msgs/srv/IsPathValid",
    [](nav2_msgs::srv::IsPathValid::Request & request) {
      request.max_cost = 100;
      request.consider_unknown_as_obstacle = true;
      request.layer_name = "global_costmap";
      request.stop_at_first_collision = false;
      request.max_lookahead_distance = 2.5;
    },
    [](const nav2_msgs::srv::IsPathValid::Request & request) {
      check(
        request.max_cost == 100 && request.consider_unknown_as_obstacle &&
        request.layer_name == "global_costmap" && !request.stop_at_first_collision,
        "nav2_msgs/srv/IsPathValid: the scalar request fields arrived");
      check(
        request.max_lookahead_distance == 2.5,
        "nav2_msgs/srv/IsPathValid: max_lookahead_distance arrived");
    },
    [](nav2_msgs::srv::IsPathValid::Response & response) {
      response.success = true;
      response.is_valid = false;
      response.invalid_pose_indices = {1, 3};
    },
    [](const nav2_msgs::srv::IsPathValid::Response & response) {
      check(
        response.success && !response.is_valid &&
        response.invalid_pose_indices.size() == 2 &&
        response.invalid_pose_indices[1] == 3,
        "nav2_msgs/srv/IsPathValid: success, is_valid and the int32[] arrived");
    });

  roundTrip<nav2_msgs::srv::LoadMap>(
    "/test/load_map", "nav2_msgs/srv/LoadMap",
    [](nav2_msgs::srv::LoadMap::Request & request) {
      request.map_url = "file:///C:/maps/room.yaml";
    },
    [](const nav2_msgs::srv::LoadMap::Request & request) {
      check(
        request.map_url == "file:///C:/maps/room.yaml",
        "nav2_msgs/srv/LoadMap: the map_url arrived");
    },
    [](nav2_msgs::srv::LoadMap::Response & response) {
      response.result = nav2_msgs::srv::LoadMap::Response::RESULT_SUCCESS;
      response.map.header.frame_id = "map";
      response.map.info.resolution = 0.05f;
      response.map.info.width = 2;
      response.map.info.height = 2;
      response.map.data = {0, 100, -1, 0};
    },
    [](const nav2_msgs::srv::LoadMap::Response & response) {
      check(
        response.result == nav2_msgs::srv::LoadMap::Response::RESULT_SUCCESS,
        "nav2_msgs/srv/LoadMap: result is RESULT_SUCCESS");
      check(
        response.map.data.size() == 4 && response.map.data[1] == 100 &&
        response.map.data[2] == -1,
        "nav2_msgs/srv/LoadMap: the map that comes with a success arrived");
    });

  roundTrip<nav2_msgs::srv::SaveMap>(
    "/test/nav2_save_map", "nav2_msgs/srv/SaveMap",
    [](nav2_msgs::srv::SaveMap::Request & request) {
      request.map_topic = "/map";
      request.map_url = "C:/maps/room";
      request.image_format = "pgm";
      request.map_mode = "trinary";
      request.free_thresh = 0.25f;
      request.occupied_thresh = 0.65f;
    },
    [](const nav2_msgs::srv::SaveMap::Request & request) {
      check(
        request.map_topic == "/map" && request.map_url == "C:/maps/room" &&
        request.image_format == "pgm" && request.map_mode == "trinary",
        "nav2_msgs/srv/SaveMap: the four strings arrived");
      check(
        request.free_thresh == 0.25f && request.occupied_thresh == 0.65f,
        "nav2_msgs/srv/SaveMap: the two thresholds arrived");
    },
    [](nav2_msgs::srv::SaveMap::Response & response) {
      response.result = true;
    },
    [](const nav2_msgs::srv::SaveMap::Response & response) {
      check(response.result, "nav2_msgs/srv/SaveMap: the bool result arrived");
    });

  // The collision monitor's zone pair. The request carries a nested message
  // (`ExclusionZoneDescription`) whose `points` is itself a list of messages -
  // the deepest payload in this table - and the answer carries a string.
  roundTrip<nav2_msgs::srv::AddExclusionZone>(
    "/test/add_exclusion_zone", "nav2_msgs/srv/AddExclusionZone",
    [](nav2_msgs::srv::AddExclusionZone::Request & request) {
      request.zone.zone_name = "keepout_a";
      request.zone.type = "polygon";
      request.zone.frame_id = "map";
      request.zone.points = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.5f, 0.0f}};
      request.zone.radius = 0.45;
      request.zone.min_height = -0.1;
      request.zone.max_height = 2.0;
      request.zone.enabled = true;
      request.zone.visualize = false;
      request.zone.frame_hold_timeout = 2.5;
    },
    [](const nav2_msgs::srv::AddExclusionZone::Request & request) {
      check(
        request.zone.zone_name == "keepout_a" && request.zone.type == "polygon",
        "nav2_msgs/srv/AddExclusionZone: the zone's name and type arrived");
      check(
        request.zone.points.size() == 2 && request.zone.points[1].x == 1.0f &&
        request.zone.points[1].y == 0.5f,
        "nav2_msgs/srv/AddExclusionZone: the nested Point32 list arrived");
      check(
        request.zone.radius == 0.45 && request.zone.min_height == -0.1 &&
        request.zone.frame_hold_timeout == 2.5,
        "nav2_msgs/srv/AddExclusionZone: the float64 fields survived");
      check(
        request.zone.enabled && !request.zone.visualize,
        "nav2_msgs/srv/AddExclusionZone: the two bools arrived the right way round");
    },
    [](nav2_msgs::srv::AddExclusionZone::Response & response) {
      response.success = true;
      response.message = "zone keepout_a added";
    },
    [](const nav2_msgs::srv::AddExclusionZone::Response & response) {
      check(
        response.success && response.message == "zone keepout_a added",
        "nav2_msgs/srv/AddExclusionZone: success and the message came back");
    });

  roundTrip<nav2_msgs::srv::RemoveExclusionZone>(
    "/test/remove_exclusion_zone", "nav2_msgs/srv/RemoveExclusionZone",
    [](nav2_msgs::srv::RemoveExclusionZone::Request & request) {
      request.zone_name = "keepout_a";
      request.remove_all = false;
    },
    [](const nav2_msgs::srv::RemoveExclusionZone::Request & request) {
      check(
        request.zone_name == "keepout_a" && !request.remove_all,
        "nav2_msgs/srv/RemoveExclusionZone: the name arrived and remove_all stayed false");
    },
    [](nav2_msgs::srv::RemoveExclusionZone::Response & response) {
      response.success = false;
      response.message = "no zone named keepout_a";
    },
    [](const nav2_msgs::srv::RemoveExclusionZone::Response & response) {
      check(
        !response.success && response.message == "no zone named keepout_a",
        "nav2_msgs/srv/RemoveExclusionZone: a refusal survives the round trip");
    });

  // The object servers' shape trio: two lists of messages in, one bool out,
  // and the same two lists back out of `GetShapes`.
  const unique_identifier_msgs::msg::UUID circle_id = [] {
      unique_identifier_msgs::msg::UUID uuid;
      uuid.uuid = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
      return uuid;
    }();

  roundTrip<nav2_msgs::srv::AddShapes>(
    "/test/add_shapes", "nav2_msgs/srv/AddShapes",
    [circle_id](nav2_msgs::srv::AddShapes::Request & request) {
      nav2_msgs::msg::CircleObject circle;
      circle.header.frame_id = "map";
      circle.uuid = circle_id;
      circle.center.x = 2.0f;
      circle.center.y = -1.5f;
      circle.radius = 0.35f;
      circle.fill = true;
      circle.value = -1;
      request.circles.push_back(circle);

      nav2_msgs::msg::PolygonObject polygon;
      polygon.header.frame_id = "map";
      polygon.points = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
      polygon.closed = true;
      polygon.value = 100;
      request.polygons.push_back(polygon);
    },
    [](const nav2_msgs::srv::AddShapes::Request & request) {
      check(
        request.circles.size() == 1 && request.polygons.size() == 1,
        "nav2_msgs/srv/AddShapes: both message lists arrived");
      check(
        request.circles[0].uuid.uuid[0] == 0x01 && request.circles[0].uuid.uuid[15] == 0x10,
        "nav2_msgs/srv/AddShapes: the circle's 16-byte uuid arrived");
      check(
        request.circles[0].center.x == 2.0f && request.circles[0].radius == 0.35f &&
        request.circles[0].fill && request.circles[0].value == -1,
        "nav2_msgs/srv/AddShapes: the circle's geometry and value arrived");
      check(
        request.polygons[0].points.size() == 3 && request.polygons[0].closed &&
        request.polygons[0].value == 100,
        "nav2_msgs/srv/AddShapes: the polygon's three points and value arrived");
    },
    [](nav2_msgs::srv::AddShapes::Response & response) {
      response.success = true;
    },
    [](const nav2_msgs::srv::AddShapes::Response & response) {
      check(response.success, "nav2_msgs/srv/AddShapes: success came back true");
    });

  roundTrip<nav2_msgs::srv::RemoveShapes>(
    "/test/remove_shapes", "nav2_msgs/srv/RemoveShapes",
    [circle_id](nav2_msgs::srv::RemoveShapes::Request & request) {
      request.all_objects = true;
      request.uuids.push_back(circle_id);
    },
    [](const nav2_msgs::srv::RemoveShapes::Request & request) {
      check(
        request.all_objects && request.uuids.size() == 1 &&
        request.uuids[0].uuid[0] == 0x01,
        "nav2_msgs/srv/RemoveShapes: all_objects and the uuid list arrived");
    },
    [](nav2_msgs::srv::RemoveShapes::Response & response) {
      response.success = false;
    },
    [](const nav2_msgs::srv::RemoveShapes::Response & response) {
      check(!response.success, "nav2_msgs/srv/RemoveShapes: a false success is not lost");
    });

  // An empty request, and the only answer in this table that hands back two
  // lists of messages at once.
  roundTrip<nav2_msgs::srv::GetShapes>(
    "/test/get_shapes", "nav2_msgs/srv/GetShapes",
    [](nav2_msgs::srv::GetShapes::Request &) {},
    ignoreRequest<nav2_msgs::srv::GetShapes>(),
    [](nav2_msgs::srv::GetShapes::Response & response) {
      nav2_msgs::msg::CircleObject circle;
      circle.center.x = -3.25f;
      circle.radius = 1.0f;
      circle.value = 1;
      response.circles.push_back(circle);

      nav2_msgs::msg::PolygonObject polygon;
      polygon.points = {{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}};
      polygon.value = 0;
      response.polygons.push_back(polygon);
    },
    [](const nav2_msgs::srv::GetShapes::Response & response) {
      check(
        response.circles.size() == 1 && response.polygons.size() == 1,
        "nav2_msgs/srv/GetShapes: both answer lists arrived");
      check(
        response.circles[0].center.x == -3.25f && response.circles[0].radius == 1.0f &&
        response.circles[0].value == 1,
        "nav2_msgs/srv/GetShapes: the circle's fields arrived");
      check(
        response.polygons[0].points.size() == 2 && !response.polygons[0].closed,
        "nav2_msgs/srv/GetShapes: the polygon's points arrived, closed staying false");
    });

  // The route server's graph editing: two `uint16` lists and a nested
  // `EdgeCost` list, which together are the widest request here.
  roundTrip<nav2_msgs::srv::DynamicEdges>(
    "/test/dynamic_edges", "nav2_msgs/srv/DynamicEdges",
    [](nav2_msgs::srv::DynamicEdges::Request & request) {
      request.closed_edges = {1, 7, 300};
      request.opened_edges = {9};
      nav2_msgs::msg::EdgeCost adjusted;
      adjusted.edgeid = 5;
      adjusted.cost = 2.5f;
      request.adjust_edges.push_back(adjusted);
    },
    [](const nav2_msgs::srv::DynamicEdges::Request & request) {
      check(
        request.closed_edges.size() == 3 && request.closed_edges[2] == 300,
        "nav2_msgs/srv/DynamicEdges: the closed edge ids arrived as uint16");
      check(
        request.opened_edges.size() == 1 && request.opened_edges[0] == 9,
        "nav2_msgs/srv/DynamicEdges: the opened edge ids arrived");
      check(
        request.adjust_edges.size() == 1 && request.adjust_edges[0].edgeid == 5 &&
        request.adjust_edges[0].cost == 2.5f,
        "nav2_msgs/srv/DynamicEdges: the nested EdgeCost arrived");
    },
    [](nav2_msgs::srv::DynamicEdges::Response & response) {
      response.success = true;
    },
    [](const nav2_msgs::srv::DynamicEdges::Response & response) {
      check(response.success, "nav2_msgs/srv/DynamicEdges: success came back true");
    });

  roundTrip<nav2_msgs::srv::SetRouteGraph>(
    "/test/set_route_graph", "nav2_msgs/srv/SetRouteGraph",
    [](nav2_msgs::srv::SetRouteGraph::Request & request) {
      request.graph_filepath = "C:/maps/route.geojson";
    },
    [](const nav2_msgs::srv::SetRouteGraph::Request & request) {
      check(
        request.graph_filepath == "C:/maps/route.geojson",
        "nav2_msgs/srv/SetRouteGraph: the geojson path arrived");
    },
    [](nav2_msgs::srv::SetRouteGraph::Response & response) {
      response.success = false;
    },
    [](const nav2_msgs::srv::SetRouteGraph::Response & response) {
      check(!response.success, "nav2_msgs/srv/SetRouteGraph: a missing file reports false");
    });

  roundTrip<nav2_msgs::srv::ReloadDockDatabase>(
    "/test/reload_dock_database", "nav2_msgs/srv/ReloadDockDatabase",
    [](nav2_msgs::srv::ReloadDockDatabase::Request & request) {
      request.filepath = "C:/maps/docks.yaml";
    },
    [](const nav2_msgs::srv::ReloadDockDatabase::Request & request) {
      check(
        request.filepath == "C:/maps/docks.yaml",
        "nav2_msgs/srv/ReloadDockDatabase: the yaml path arrived");
    },
    [](nav2_msgs::srv::ReloadDockDatabase::Response & response) {
      response.success = true;
    },
    [](const nav2_msgs::srv::ReloadDockDatabase::Response & response) {
      check(response.success, "nav2_msgs/srv/ReloadDockDatabase: success came back true");
    });

  // The registry has to answer for the names `ros2 service call` and the
  // daemon build out of a service type.
  check(
    ros2_bridge::service_request_type("std_srvs/Trigger") == "std_srvs/srv/Trigger_Request",
    "type helper: std_srvs/Trigger -> std_srvs/srv/Trigger_Request");
  check(
    ros2_bridge::service_response_type("std_srvs/srv/Trigger") == "std_srvs/srv/Trigger_Response",
    "type helper: std_srvs/srv/Trigger -> std_srvs/srv/Trigger_Response");
  check(
    ros2_bridge::find_service_codec("std_srvs/Trigger") != nullptr,
    "lookup: the abbreviated spelling resolves");
  check(
    ros2_bridge::find_service_codec("nav_msgs/srv/GetPlan") == nullptr,
    "lookup: a type the bridge cannot convert is refused");
  check(
    ros2_bridge::find_service_codec("nav2_msgs/SetInitialPose") ==
    ros2_bridge::find_service_codec("nav2_msgs/srv/SetInitialPose"),
    "lookup: nav2_msgs/SetInitialPose resolves to the same codec");
  check(
    ros2_bridge::supported_services().size() == 38,
    "lookup: the registry holds all thirty-eight service types");

  std::printf(
    "\n%d checks, %d failure(s)%s\n", g_checks, g_failures,
    g_failures == 0 ? " - ALL CHECKS PASSED" : "");
  return g_failures == 0 ? 0 : 1;
}
