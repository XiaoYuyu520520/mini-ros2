#ifndef RCLCPP__TIMER_HPP_
#define RCLCPP__TIMER_HPP_

#include <atomic>
#include <chrono>
#include <memory>
#include <utility>

#include "rclcpp/callback_group.hpp"
#include "rclcpp/duration.hpp"
#include "rclcpp/macros.hpp"

namespace rclcpp
{

class TimerBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(TimerBase)

  virtual ~TimerBase() = default;
  virtual void execute_if_ready() = 0;
  virtual bool is_ready() = 0;
  virtual void cancel() {canceled_.store(true);}
  virtual bool is_canceled() {return canceled_.load();}
  virtual void reset() = 0;
  virtual std::chrono::nanoseconds time_until_trigger() = 0;

protected:
  std::atomic<bool> canceled_{false};
};

template<typename CallbackT>
class WallTimer : public TimerBase
{
public:
  using SharedPtr = std::shared_ptr<WallTimer<CallbackT>>;

  WallTimer(std::chrono::nanoseconds period, CallbackT callback)
  : period_(period), callback_(std::move(callback)),
    next_(std::chrono::steady_clock::now() + period_)
  {
  }

  bool is_ready() override
  {
    return !canceled_.load() && std::chrono::steady_clock::now() >= next_;
  }

  void execute_if_ready() override
  {
    if (!is_ready()) {
      return;
    }
    next_ = std::chrono::steady_clock::now() + period_;
    callback_();
  }

  void reset() override
  {
    next_ = std::chrono::steady_clock::now() + period_;
  }

  std::chrono::nanoseconds time_until_trigger() override
  {
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_) {
      return std::chrono::nanoseconds(0);
    }
    return std::chrono::duration_cast<std::chrono::nanoseconds>(next_ - now);
  }

private:
  std::chrono::nanoseconds period_;
  CallbackT callback_;
  std::chrono::steady_clock::time_point next_;
};

// Generic timer created from a rclcpp::Duration period.
template<typename CallbackT>
class GenericTimer : public WallTimer<CallbackT>
{
public:
  GenericTimer(const rclcpp::Duration & period, CallbackT callback)
  : WallTimer<CallbackT>(std::chrono::nanoseconds(period.nanoseconds()), std::move(callback))
  {
  }
};

}  // namespace rclcpp

#endif  // RCLCPP__TIMER_HPP_
