#ifndef RCLCPP__CLOCK_TYPE_HPP_
#define RCLCPP__CLOCK_TYPE_HPP_

// `rcl_clock_type_t`, the clock kind a `rclcpp::Time` carries.
//
// rcl defines this in `rcl/types.h`, as a global-scope C typedef - not in a
// namespace - which is why it is spelled that way here. It lives in its own
// header because both `rclcpp/time.hpp` and `rclcpp/clock.hpp` need it and
// `time.hpp` is what `clock.hpp` includes: the clock type is a property of a
// time, and of the clock that produced it.
//
// The shim has one timeline, not three. A `Time` is nanoseconds since an
// epoch, whoever stamped it, and nothing in the ported code does arithmetic
// that depends on which clock those nanoseconds came from. The value is
// carried because callers read it - `nav2_behavior_tree`'s `LoopRate` asks
// whether a clock is a ROS clock, `nav2_collision_monitor` and
// `nav2_velocity_smoother` stamp their own state with their node's - and
// because `rclcpp::Time` is constructed with it from a message header
// (`rclcpp::Time(msg.header.stamp, curr_time.get_clock_type())`).

typedef enum rcl_clock_type_e
{
  RCL_CLOCK_UNINITIALIZED = 0,
  RCL_ROS_TIME,
  RCL_SYSTEM_TIME,
  RCL_STEADY_TIME,
} rcl_clock_type_t;

#endif  // RCLCPP__CLOCK_TYPE_HPP_
