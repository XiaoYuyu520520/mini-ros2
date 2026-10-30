#ifndef RCLCPP_ACTION__TYPES_HPP_
#define RCLCPP_ACTION__TYPES_HPP_

#include <array>
#include <cstdint>

#include "action_msgs/msg/goal_status.hpp"

namespace rclcpp_action
{

using GoalUUID = std::array<uint8_t, 16>;

/// The goal's lifecycle state, exactly as the message that carries it on the
/// wire defines it. Re-exported rather than copied so there is one place the
/// seven codes come from - `bt_action_node` compares a handle's status
/// against `action_msgs::msg::GoalStatus`, and the two names have to agree.
using GoalStatus = action_msgs::msg::GoalStatus;

enum class GoalResponse : int8_t
{
  REJECT = 0,
  ACCEPT_AND_EXECUTE = 1,
  ACCEPT_AND_DEFER = 2,
};

enum class CancelResponse : int8_t
{
  REJECT = 0,
  ACCEPT = 1,
};

enum class ResultCode : int8_t
{
  UNKNOWN = 0,
  SUCCEEDED = 1,
  ABORTED = 2,
  CANCELED = 3,
};

}  // namespace rclcpp_action

#endif  // RCLCPP_ACTION__TYPES_HPP_
