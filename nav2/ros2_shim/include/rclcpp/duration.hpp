#ifndef RCLCPP__DURATION_HPP_
#define RCLCPP__DURATION_HPP_

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "builtin_interfaces/msg/duration.hpp"

namespace rclcpp
{

class Duration
{
public:
  Duration() = default;

  explicit Duration(int64_t nanoseconds)
  : nanoseconds_(nanoseconds)
  {
  }

  Duration(int32_t seconds, uint32_t nanoseconds)
  : nanoseconds_(static_cast<int64_t>(seconds) * 1000000000LL + nanoseconds)
  {
  }

  Duration(const builtin_interfaces::msg::Duration & msg)
  : nanoseconds_(
      static_cast<int64_t>(msg.sec) * 1000000000LL + msg.nanosec)
  {
  }

  explicit Duration(std::chrono::nanoseconds ns)
  : nanoseconds_(ns.count())
  {
  }

  template<class Rep, class Period>
  Duration(const std::chrono::duration<Rep, Period> & d)  // NOLINT
  : nanoseconds_(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count())
  {
  }

  static Duration from_seconds(double seconds)
  {
    return Duration(static_cast<int64_t>(std::llround(seconds * 1e9)));
  }

  static Duration from_nanoseconds(int64_t nanoseconds)
  {
    return Duration(nanoseconds);
  }

  // Upstream only declares the `double` overload. `from_seconds(0)` is used
  // constantly by slam_toolbox, so an exact-match template replaces a second
  // `int64_t` overload: `0` is an `int`, and asking it to choose between
  // `double` and `int64_t` conversions is ambiguous.
  template<
    typename IntegerT,
    std::enable_if_t<std::is_integral_v<IntegerT> && !std::is_same_v<IntegerT, bool>, int> = 0>
  static Duration from_seconds(IntegerT seconds)
  {
    return Duration(static_cast<int64_t>(seconds), 0);
  }

  static Duration max()
  {
    return Duration(std::numeric_limits<int64_t>::max());
  }

  double seconds() const
  {
    return static_cast<double>(nanoseconds_) / 1e9;
  }

  int64_t nanoseconds() const
  {
    return nanoseconds_;
  }

  // Upstream makes this a template so `duration.to_chrono<std::chrono::milliseconds>()`
  // reads in whatever unit the caller wants. The default keeps the no-argument
  // form working for the callers that just want nanoseconds back.
  template<class DurationT = std::chrono::nanoseconds>
  DurationT to_chrono() const
  {
    return std::chrono::duration_cast<DurationT>(std::chrono::nanoseconds(nanoseconds_));
  }

  builtin_interfaces::msg::Duration to_message() const
  {
    builtin_interfaces::msg::Duration msg;
    msg.sec = static_cast<int32_t>(nanoseconds_ / 1000000000LL);
    msg.nanosec = static_cast<uint32_t>(nanoseconds_ % 1000000000LL);
    return msg;
  }

  operator builtin_interfaces::msg::Duration() const
  {
    return to_message();
  }

  Duration operator+(const Duration & other) const
  {
    return Duration(nanoseconds_ + other.nanoseconds_);
  }

  Duration operator-(const Duration & other) const
  {
    return Duration(nanoseconds_ - other.nanoseconds_);
  }

  Duration operator*(double factor) const
  {
    return Duration(static_cast<int64_t>(static_cast<double>(nanoseconds_) * factor));
  }

  bool operator==(const Duration & other) const {return nanoseconds_ == other.nanoseconds_;}
  bool operator!=(const Duration & other) const {return nanoseconds_ != other.nanoseconds_;}
  bool operator<(const Duration & other) const {return nanoseconds_ < other.nanoseconds_;}
  bool operator<=(const Duration & other) const {return nanoseconds_ <= other.nanoseconds_;}
  bool operator>(const Duration & other) const {return nanoseconds_ > other.nanoseconds_;}
  bool operator>=(const Duration & other) const {return nanoseconds_ >= other.nanoseconds_;}

private:
  int64_t nanoseconds_ = 0;
};

}  // namespace rclcpp

#endif  // RCLCPP__DURATION_HPP_
