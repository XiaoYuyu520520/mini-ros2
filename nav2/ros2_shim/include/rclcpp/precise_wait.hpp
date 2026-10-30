#ifndef RCLCPP__PRECISE_WAIT_HPP_
#define RCLCPP__PRECISE_WAIT_HPP_

// A wall-clock wait that actually keeps the deadline it is given.
//
// `std::this_thread::sleep_until` is only as precise as the OS timer tick,
// and on Windows that tick is 15.6 ms unless something raises it. A 20 Hz
// loop (50 ms) does not notice; a 1000 Hz publish loop asks to sleep 1 ms and
// gets 15.6, so it runs at ~64 Hz. `ros2 topic pub -r 1000` did exactly that.
//
// The wait is split: sleep the bulk of the interval (one tick of margin), then
// yield until the deadline. At 1000 Hz the whole interval is short, so it is
// all "yield", which is what a 1 kHz loop has to do - there is no room for a
// sleep that overshoots. At low rates the cost is one tick of spinning.
//
// A caller that has fallen far behind is not bursted: `wait_until` returns
// immediately when the deadline is already past, and the loop is expected to
// resynchronise its own schedule (see the publish loop in ros2_cli.cpp).

#include <chrono>
#include <thread>

namespace rclcpp
{
namespace shim
{

inline void wait_until(std::chrono::steady_clock::time_point deadline)
{
  using std::chrono::milliseconds;
  for (;;) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      return;
    }
    const auto remaining = deadline - now;
    if (remaining > milliseconds(2)) {
      // Leave a tick of margin so the sleep does not overshoot the deadline
      // by a whole timer tick.
      std::this_thread::sleep_for(remaining - milliseconds(1));
    } else {
      // Too short to sleep: a 1 ms sleep is a 15.6 ms sleep on Windows. Yield
      // so another ready thread can run, then check the clock again.
      std::this_thread::yield();
    }
  }
}

inline void sleep_for(std::chrono::steady_clock::duration interval)
{
  wait_until(std::chrono::steady_clock::now() + interval);
}

}  // namespace shim
}  // namespace rclcpp

#endif  // RCLCPP__PRECISE_WAIT_HPP_