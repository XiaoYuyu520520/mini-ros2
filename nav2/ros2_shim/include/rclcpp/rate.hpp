#ifndef RCLCPP__RATE_HPP_
#define RCLCPP__RATE_HPP_

#include <chrono>
#include <stdexcept>
#include <thread>

#include "rclcpp/clock.hpp"
#include "rclcpp/duration.hpp"
#include "rclcpp/precise_wait.hpp"

namespace rclcpp
{

class Rate
{
public:
  explicit Rate(const Duration & period)
  : period_(period)
  {
    if (period.nanoseconds() <= 0) {
      throw std::invalid_argument("rate period must be positive");
    }
    last_ = std::chrono::steady_clock::now();
  }

  Rate(double rate, rclcpp::Clock::SharedPtr clock)
  : Rate(Duration::from_seconds(1.0 / rate))
  {
    (void)clock;
  }

  explicit Rate(double rate)
  : Rate(Duration::from_seconds(1.0 / rate))
  {
  }

  bool sleep()
  {
    last_ += std::chrono::nanoseconds(period_.nanoseconds());
    // `sleep_until` alone caps out at the OS timer tick (15.6 ms on Windows),
    // which turns a 1 kHz rate into ~64 Hz. `wait_until` sleeps the bulk and
    // spins the last tick, so the rate is the one the caller asked for.
    rclcpp::shim::wait_until(last_);
    return true;
  }

  double get_rate() const {return 1.0 / period_.seconds();}

  void reset() {last_ = std::chrono::steady_clock::now();}

  Duration period() const {return period_;}

private:
  Duration period_;
  std::chrono::steady_clock::time_point last_;
};

class WallRate : public Rate
{
public:
  explicit WallRate(const Duration & period)
  : Rate(period)
  {
  }

  explicit WallRate(double rate)
  : Rate(Duration::from_seconds(1.0 / rate))
  {
  }
};

}  // namespace rclcpp

#endif  // RCLCPP__RATE_HPP_
