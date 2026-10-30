#ifndef TF2_ROS__BUFFER_INTERFACE_HPP_
#define TF2_ROS__BUFFER_INTERFACE_HPP_

#include "builtin_interfaces/msg/time.hpp"
#include "rclcpp/duration.hpp"
#include "rclcpp/time.hpp"
#include "tf2/buffer_core.hpp"
#include "tf2/time.hpp"

namespace tf2_ros
{

class BufferInterface
{
public:
  virtual ~BufferInterface() = default;
};

inline builtin_interfaces::msg::Time toMsg(const tf2::TimePoint & time)
{
  return tf2::timePointToRosTime(time);
}

inline tf2::TimePoint fromMsg(const builtin_interfaces::msg::Time & time)
{
  return tf2::timePointFromRosTime(time);
}

// Upstream declares these four in this header and defines them in tf2_ros'
// own translation unit. The ported navigation2 code calls them straight from
// headers (nav2_costmap_2d's range_sensor_layer is one), so they are `inline`
// here instead of living in a library nobody would link against.
inline tf2::TimePoint fromRclcpp(const rclcpp::Time & time)
{
  return tf2::TimePoint(std::chrono::nanoseconds(time.nanoseconds()));
}

inline rclcpp::Time toRclcpp(const tf2::TimePoint & time)
{
  return rclcpp::Time(static_cast<int64_t>(time.time_since_epoch().count()));
}

inline tf2::Duration fromRclcpp(const rclcpp::Duration & duration)
{
  return std::chrono::nanoseconds(duration.nanoseconds());
}

inline rclcpp::Duration toRclcpp(const tf2::Duration & duration)
{
  return rclcpp::Duration(static_cast<int64_t>(duration.count()));
}

}  // namespace tf2_ros

#endif  // TF2_ROS__BUFFER_INTERFACE_HPP_
