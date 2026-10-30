#ifndef RCLCPP__UTILITIES_HPP_
#define RCLCPP__UTILITIES_HPP_

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <string>
#include <thread>

#include "rclcpp/context.hpp"

namespace rclcpp
{

namespace detail
{
inline std::atomic<bool> & ok_flag()
{
  static std::atomic<bool> flag{false};
  return flag;
}
}  // namespace detail

inline void init(int argc, char const * const * argv, const std::string & = "")
{
  (void)argc;
  (void)argv;
  detail::ok_flag().store(true);
}

inline bool ok()
{
  return detail::ok_flag().load();
}

inline bool shutdown()
{
  // Upstream shuts the context down here, and that is what fires every node's
  // pre-shutdown callback. nav2 has real work in them - deactivating and
  // cleaning up lifecycle nodes (which is what joins `Costmap2DROS`'s map
  // update thread before its `std::thread` member is destroyed) and tearing
  // the lifecycle manager's bond map down - so the shim has to run them
  // before the node objects go away (see rclcpp/context.hpp).
  //
  // They run before the ok flag is cleared, the way upstream has a valid
  // context while its callbacks run, so teardown that polls `rclcpp::ok()`
  // still sees the world it was expecting.
  Context::get_global()->run_pre_shutdown_callbacks();

  const bool was_ok = detail::ok_flag().exchange(false);
  return was_ok;
}

inline void sleep_for(const std::chrono::nanoseconds & duration)
{
  std::this_thread::sleep_for(duration);
}

template <class RepT, class PeriodT>
inline void sleep_for(const std::chrono::duration<RepT, PeriodT> & duration)
{
  std::this_thread::sleep_for(duration);
}

inline void sleep_until(const std::chrono::nanoseconds & until)
{
  std::this_thread::sleep_until(std::chrono::steady_clock::time_point(until));
}

template <typename DurationT>
inline void sleep_until(const DurationT & until)
{
  std::this_thread::sleep_until(until);
}

inline void on_shutdown(void (*)())
{
}

class SignalHandler
{
public:
  explicit SignalHandler(bool = true) {}
  void uninstall() {}
};

// ---- time source helpers -------------------------------------------------
inline void set_use_sim_time(bool) {}

}  // namespace rclcpp

#endif  // RCLCPP__UTILITIES_HPP_
