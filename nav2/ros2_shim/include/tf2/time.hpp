#ifndef TF2__TIME_HPP_
#define TF2__TIME_HPP_

#include <chrono>
#include <cstdint>

#include "builtin_interfaces/msg/time.hpp"

namespace tf2
{

using Duration = std::chrono::nanoseconds;
using TimePoint = std::chrono::time_point<std::chrono::system_clock, Duration>;

static const TimePoint TimePointZero = TimePoint(Duration::zero());

inline TimePoint get_now()
{
  return std::chrono::time_point_cast<Duration>(std::chrono::system_clock::now());
}

inline double durationToSec(const Duration & d)
{
  return std::chrono::duration_cast<std::chrono::duration<double>>(d).count();
}

inline Duration durationFromSec(double sec)
{
  return std::chrono::duration_cast<Duration>(std::chrono::duration<double>(sec));
}

inline TimePoint timePointFromRosTime(const builtin_interfaces::msg::Time & t)
{
  return TimePoint(Duration(static_cast<int64_t>(t.sec) * 1000000000LL + t.nanosec));
}

inline builtin_interfaces::msg::Time timePointToRosTime(const TimePoint & t)
{
  builtin_interfaces::msg::Time msg;
  const auto ns = t.time_since_epoch().count();
  msg.sec = static_cast<int32_t>(ns / 1000000000LL);
  msg.nanosec = static_cast<uint32_t>(ns % 1000000000LL);
  return msg;
}

}  // namespace tf2

#endif  // TF2__TIME_HPP_
