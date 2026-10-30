#ifndef RCLCPP__CLOCK_HPP_
#define RCLCPP__CLOCK_HPP_

#include <atomic>
#include <chrono>
#include <thread>

#include "rclcpp/clock_type.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/time.hpp"

namespace rclcpp
{

class Clock
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(Clock)

  enum ClockType
  {
    ROS_TIME,
    SYSTEM_TIME,
    STEADY_TIME,
  };

  explicit Clock(ClockType type = SYSTEM_TIME)
  : type_(type)
  {
  }

  explicit Clock(rcl_clock_type_t type)
  : type_(from_rcl_type(type))
  {
  }

  Time now() const
  {
    if (use_sim_time_.load(std::memory_order_relaxed)) {
      // A node running on simulated time reads the clock its `/clock`
      // subscription keeps up to date, and reports the epoch until the first
      // message lands - the same thing rclcpp does, and the reason a server
      // started without a bridge stamps the epoch rather than quietly mixing
      // wall time into a simulation timeline.
      return Time(sim_time_ns_.load(std::memory_order_relaxed));
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
    return Time(static_cast<int64_t>(ns));
  }

  ClockType get_clock_type() const
  {
    return use_sim_time_.load(std::memory_order_relaxed) ? ROS_TIME : type_;
  }

  bool ros_time_is_active() const
  {
    return get_clock_type() == ROS_TIME;
  }

  // `use_sim_time` is on the node's ROS clock in rclcpp, and is what switches
  // `now()` over to the value `/clock` carries.
  void set_use_sim_time(bool value)
  {
    use_sim_time_.store(value, std::memory_order_relaxed);
  }

  bool get_use_sim_time() const {return use_sim_time_.load(std::memory_order_relaxed);}

  /// Block for `relative_time`. On simulated time this waits for `/clock` to
  /// carry the deadline past, which is what upstream's ROS clock does too.
  ///
  /// The wall clock is a second way out, and it is the one that matters here.
  /// Upstream can afford to return only when the simulation says so because a
  /// stalled `/clock` costs it a stalled node and nothing more: its ROS clock
  /// is advanced by `rcl`, off the node's own subscription, and the process
  /// keeps polling that subscription on whatever thread is not waiting. This
  /// port has the same arrangement - `set_sim_time()` below is called from the
  /// `/clock` subscription - which means a wait for simulated time can only
  /// end if somebody else polls `/clock`, and the thread that is asked to wait
  /// is not always somebody else. Bound it by the same duration measured on
  /// the wall clock: a live simulation ends the wait on the first condition,
  /// exactly as before, and a `relative_time` spent waiting on a clock nobody
  /// is feeding ends on the second one instead of never.
  template<typename RepT, typename PeriodT>
  bool sleep_for(std::chrono::duration<RepT, PeriodT> relative_time)
  {
    if (!use_sim_time_.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(relative_time);
      return true;
    }
    const int64_t span =
      std::chrono::duration_cast<std::chrono::nanoseconds>(relative_time).count();
    const int64_t deadline = sim_time_ns_.load(std::memory_order_relaxed) + span;
    const auto wall_deadline = std::chrono::steady_clock::now() + relative_time;
    while (sim_time_ns_.load(std::memory_order_relaxed) < deadline) {
      if (std::chrono::steady_clock::now() >= wall_deadline) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
  }

  void set_sim_time(int64_t nanoseconds)
  {
    sim_time_ns_.store(nanoseconds, std::memory_order_relaxed);
  }

  int64_t get_sim_time() const {return sim_time_ns_.load(std::memory_order_relaxed);}

private:
  static ClockType from_rcl_type(rcl_clock_type_t type)
  {
    switch (type) {
      case RCL_ROS_TIME: return ROS_TIME;
      case RCL_STEADY_TIME: return STEADY_TIME;
      case RCL_SYSTEM_TIME: return SYSTEM_TIME;
      case RCL_CLOCK_UNINITIALIZED: break;
    }
    return SYSTEM_TIME;
  }

  ClockType type_;
  std::atomic<bool> use_sim_time_{false};
  std::atomic<int64_t> sim_time_ns_{0};
};

}  // namespace rclcpp

#endif  // RCLCPP__CLOCK_HPP_
