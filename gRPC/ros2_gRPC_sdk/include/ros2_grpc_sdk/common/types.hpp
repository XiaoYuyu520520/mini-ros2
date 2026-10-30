#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ros2_grpc_sdk/export.hpp"

namespace ros2_grpc_sdk {

// Raw serialized message payload.
using Bytes = std::vector<std::uint8_t>;

// Description of a topic discovered in the ROS2 graph.
struct ROS2_GRPC_SDK_API TopicInfo {
  std::string name;
  std::string type;
  int publisher_count = 0;
  int subscriber_count = 0;
};

// A single serialized ROS2 message as transported over gRPC.
struct ROS2_GRPC_SDK_API Message {
  std::string topic;
  std::string type;
  Bytes data;
  // "cdr" for real ROS2 graphs. The mock backend uses "cdr" as well so the
  // wire format stays identical, but custom backends may use other tags.
  std::string encoding = "cdr";
  std::int64_t stamp_ns = 0;
  std::uint64_t sequence = 0;
};

// A service advertised in a ROS2 graph.
struct ROS2_GRPC_SDK_API ServiceInfo {
  std::string name;
  std::string type;
  // "local" for a service served on the agent's own machine, otherwise the
  // peer it was announced by.
  std::string source;
};

// The outcome of one `CallService` round trip.
struct ROS2_GRPC_SDK_API ServiceCallResult {
  bool success = false;
  std::string error;
  Bytes response;
  std::string encoding;
  std::int64_t elapsed_ns = 0;
};

// An action advertised in a ROS2 graph.
struct ROS2_GRPC_SDK_API ActionInfo {
  std::string name;
  std::string type;
  std::string source;
};

// How a goal is doing. The values match `action_msgs/msg/GoalStatus` so a
// number taken from this enum means the same thing in both worlds.
enum class GoalStatus : std::int32_t {
  Unknown = 0,
  Accepted = 1,
  Executing = 2,
  Canceling = 3,
  Succeeded = 4,
  Canceled = 5,
  Aborted = 6,
  Rejected = 7,
};

inline bool isTerminal(GoalStatus status) {
  return status == GoalStatus::Succeeded || status == GoalStatus::Canceled ||
         status == GoalStatus::Aborted || status == GoalStatus::Rejected;
}

// One message of an action goal's life: feedback while it runs, then the
// result on the terminal message.
struct ROS2_GRPC_SDK_API ActionFeedback {
  std::string name;
  std::string goal_id;
  Bytes feedback;
  Bytes result;
  std::string encoding;
  std::string error;
  GoalStatus status = GoalStatus::Unknown;

  bool terminal() const { return isTerminal(status); }
};

// Called for every message of a followed goal. Returning false stops the
// stream: the caller has seen enough.
using ActionFeedbackSink = std::function<bool(const ActionFeedback&)>;

}  // namespace ros2_grpc_sdk
