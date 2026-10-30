#ifndef RCLCPP__TIME_HPP_
#define RCLCPP__TIME_HPP_

#include <cstdint>
#include <string>
#include <chrono>

#include "builtin_interfaces/msg/time.hpp"
#include "rclcpp/clock_type.hpp"
#include "rclcpp/duration.hpp"

namespace rclcpp
{

class Time
{
public:
  Time() = default;

  explicit Time(int64_t nanoseconds)
  : nanoseconds_(nanoseconds)
  {
  }

  explicit Time(int seconds)
  : nanoseconds_(static_cast<int64_t>(seconds) * 1000000000LL)
  {
  }

  Time(int32_t seconds, uint32_t nanoseconds)
  : nanoseconds_(static_cast<int64_t>(seconds) * 1000000000LL + nanoseconds)
  {
  }

  Time(int32_t seconds, uint32_t nanoseconds, int)
  : nanoseconds_(static_cast<int64_t>(seconds) * 1000000000LL + nanoseconds)
  {
  }

  // The two-argument form upstream uses when a message stamp has to be turned
  // back into a `Time` that belongs to a known clock:
  // `rclcpp::Time(msg.header.stamp, curr_time.get_clock_type())`, which is how
  // nav2_collision_monitor stamps an exclusion-zone frame. The clock type is
  // remembered rather than used (see rclcpp/clock_type.hpp); the nanoseconds go
  // in the same way whichever clock produced them.
  Time(
    const builtin_interfaces::msg::Time & msg,
    rcl_clock_type_t clock_type = RCL_SYSTEM_TIME)
  : nanoseconds_(static_cast<int64_t>(msg.sec) * 1000000000LL + msg.nanosec),
    clock_type_(clock_type)
  {
  }

  explicit Time(double seconds)
  : nanoseconds_(static_cast<int64_t>(seconds * 1e9))
  {
  }

  double seconds() const
  {
    return static_cast<double>(nanoseconds_) / 1e9;
  }

  int64_t nanoseconds() const
  {
    return nanoseconds_;
  }

  rcl_clock_type_t get_clock_type() const
  {
    return clock_type_;
  }

  builtin_interfaces::msg::Time to_message() const
  {
    builtin_interfaces::msg::Time msg;
    msg.sec = static_cast<int32_t>(nanoseconds_ / 1000000000LL);
    msg.nanosec = static_cast<uint32_t>(nanoseconds_ % 1000000000LL);
    return msg;
  }

  operator builtin_interfaces::msg::Time() const
  {
    return to_message();
  }

  Duration operator-(const Time & other) const
  {
    return Duration(nanoseconds_ - other.nanoseconds_);
  }

  Time operator+(const Duration & duration) const
  {
    return Time(nanoseconds_ + duration.nanoseconds());
  }

  Time operator-(const Duration & duration) const
  {
    return Time(nanoseconds_ - duration.nanoseconds());
  }

  Time & operator+=(const Duration & duration)
  {
    nanoseconds_ += duration.nanoseconds();
    return *this;
  }

  Time & operator-=(const Duration & duration)
  {
    nanoseconds_ -= duration.nanoseconds();
    return *this;
  }

  bool operator==(const Time & other) const {return nanoseconds_ == other.nanoseconds_;}
  bool operator!=(const Time & other) const {return nanoseconds_ != other.nanoseconds_;}
  bool operator<(const Time & other) const {return nanoseconds_ < other.nanoseconds_;}
  bool operator<=(const Time & other) const {return nanoseconds_ <= other.nanoseconds_;}
  bool operator>(const Time & other) const {return nanoseconds_ > other.nanoseconds_;}
  bool operator>=(const Time & other) const {return nanoseconds_ >= other.nanoseconds_;}

private:
  int64_t nanoseconds_ = 0;
  rcl_clock_type_t clock_type_ = RCL_SYSTEM_TIME;
};

// Arithmetic between the *message* type and a Duration, which upstream rclcpp
// declares alongside the `Time` class and which callers reach for directly:
// `nav2_velocity_smoother` stamps its outgoing command with
// `cmd_vel->header.stamp + delta_time_since_last_command`, where the stamp is a
// `builtin_interfaces::msg::Time` straight out of the message it is forwarding.
// Without these the expression has no operand types in common - the shim's
// `Time` converts *to* the message type, but nothing converts the result of an
// addition back.
//
// Only the two that take a Duration are declared. Upstream also has a
// `msg::Time - msg::Time`, and adding it here makes `rclcpp::Time -
// msg::Time` ambiguous: the shim's `Time` converts both ways, so the built-in
// `Time::operator-(const Time&)` and a free `operator-(msg, msg)` each need
// exactly one user-defined conversion of the other operand and neither wins.
// `nav2_costmap_2d/src/observation_buffer.cpp:190` is written that way, so the
// subtraction stays on the `Time` class.
inline builtin_interfaces::msg::Time operator+(
  const builtin_interfaces::msg::Time & lhs, const Duration & rhs)
{
  return (Time(lhs) + rhs).to_message();
}

inline builtin_interfaces::msg::Time operator-(
  const builtin_interfaces::msg::Time & lhs, const Duration & rhs)
{
  return (Time(lhs) - rhs).to_message();
}

}  // namespace rclcpp

namespace std
{
inline std::string to_string(const rclcpp::Time & time)
{
  return std::to_string(time.seconds());
}
inline std::string to_string(const rclcpp::Duration & duration)
{
  return std::to_string(duration.seconds());
}
}  // namespace std

#endif  // RCLCPP__TIME_HPP_
