// A node on simulated time follows `/clock`.
//
// The Ubuntu ramp stamps every header with the time the simulation is at and
// publishes the same number on `/clock`. A node whose `use_sim_time` parameter
// is true has to read that number back from `now()`: the wall clock is 2026
// while a simulation starts at 0, so a component that pairs `now()` with a
// stamped message is decades away from the message it is pairing with.
//
// Four properties, in the order a server meets them:
//
//   1. `use_sim_time` defaults to false and a node with it off reads the wall
//      clock,
//   2. a node with it on reports the epoch until the first `/clock` message
//      lands - the same thing rclcpp does, rather than quietly mixing wall time
//      into a simulation timeline,
//   3. `/clock` drives `now()` once it arrives, nanoseconds included,
//   4. `get_clock_type()` follows the parameter: ROS_TIME when it is on.

#include "rclcpp/rclcpp.hpp"
#include "rosgraph_msgs/msg/clock.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

namespace
{

int failures = 0;

void check(bool condition, const std::string & what)
{
  std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
  if (!condition) {
    ++failures;
  }
}

constexpr int64_t kSecond = 1000000000LL;

}  // namespace

int main()
{
  // 1. the default: no `use_sim_time`, so the system clock answers.
  auto wall = std::make_shared<rclcpp::Node>("clock_wall");
  check(!wall->get_clock()->get_use_sim_time(), "use_sim_time defaults to false");
  check(
    wall->get_clock()->get_clock_type() == rclcpp::Clock::SYSTEM_TIME,
    "a node without it reports SYSTEM_TIME");
  check(
    wall->now().nanoseconds() > 1600000000LL * kSecond,
    "and its now() is the system clock");

  // 2. the same node with the parameter turned on, which is what a
  //    `--params-file` entry does for the nav2 stack.
  rclcpp::NodeOptions options;
  options.parameter_overrides({rclcpp::Parameter("use_sim_time", true)});
  auto sim = std::make_shared<rclcpp::Node>("clock_sim", options);
  check(sim->get_clock()->get_use_sim_time(), "use_sim_time:=true turns the clock over");
  check(
    sim->get_clock()->get_clock_type() == rclcpp::Clock::ROS_TIME,
    "and that node reports ROS_TIME");
  check(
    sim->now().nanoseconds() == 0,
    "before the first /clock message it reads the epoch, not the wall clock");

  // 3. /clock drives it. The ramp is a second node in this process, so the
  //    message travels the same shared-memory channel the bridge mirrors the
  //    Ubuntu one into.
  auto ramp = std::make_shared<rclcpp::Node>("clock_ramp");
  auto publisher = ramp->create_publisher<rosgraph_msgs::msg::Clock>("/clock", 10);

  auto publish_at = [&](int64_t nanoseconds) {
    rosgraph_msgs::msg::Clock message;
    message.clock.sec = static_cast<int32_t>(nanoseconds / kSecond);
    message.clock.nanosec = static_cast<uint32_t>(nanoseconds % kSecond);
    publisher->publish(message);
    return true;
  };
  auto spin_until = [&](int64_t nanoseconds) {
    for (int i = 0; i < 2000 && sim->now().nanoseconds() != nanoseconds; ++i) {
      ramp->spin_once();
      sim->spin_once();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return sim->now().nanoseconds() == nanoseconds;
  };

  check(publish_at(7 * kSecond + 500000000LL), "the ramp publishes /clock at 7.5 s");
  check(spin_until(7 * kSecond + 500000000LL), "and now() follows it");
  check(
    sim->now().seconds() == 7.5,
    "seconds() reports the same instant the message carried");
  check(publish_at(12345678901LL), "the ramp publishes again at 12.345678901 s");
  check(
    spin_until(12345678901LL),
    "the clock keeps following, nanoseconds included");

  // 4. the two clocks are still separate: moving the simulation did not drag
  //    the wall-clock node's now() back to 1970.
  check(
    wall->now().nanoseconds() > 1600000000LL * kSecond,
    "the wall-clock node stayed on the system clock");

  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
    failures == 1 ? "" : "s");
  return failures == 0 ? 0 : 1;
}
