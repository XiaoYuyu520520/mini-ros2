// Copyright (c) 2024 Angsa Robotics
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef NAV2_BEHAVIOR_TREE__UTILS__LOOP_RATE_HPP_
#define NAV2_BEHAVIOR_TREE__UTILS__LOOP_RATE_HPP_

#include <memory>
#include <utility>

#include "rclcpp/rclcpp.hpp"
#include "behaviortree_cpp/bt_factory.h"
#include "behaviortree_cpp/behavior_tree.h"

namespace nav2_behavior_tree
{

class LoopRate
{
public:
  LoopRate(
    const rclcpp::Duration & period, BT::Tree * tree,
    rclcpp::Clock::SharedPtr clock)
  : clock_(std::move(clock)), period_(period),
    last_interval_(clock_->now()), tree_(tree)
  {}

  // Similar to rclcpp::WallRate::sleep() but using tree_->sleep()
  bool sleep()
  {
    // Time coming into sleep
    auto now = clock_->now();
    // Time of next interval
    auto next_interval = last_interval_ + period_;
    // Detect backwards time flow
    if (now < last_interval_) {
      // Best thing to do is to set the next_interval to now + period
      next_interval = now + period_;
    }
    // Update the interval
    last_interval_ += period_;
    // If the time_to_sleep is negative or zero, don't sleep
    if (next_interval <= now) {
      // If an entire cycle was missed then reset next interval.
      // This might happen if the loop took more than a cycle.
      // Or if time jumps forward.
      if (now > next_interval + period_) {
        last_interval_ = now + period_;
      }
      // Either way do not sleep and return false
      return false;
    }
    // Sleep until the target time, preemptible by emitWakeUpSignal().
    auto wake_up = tree_->wakeUpSignal();
    // PORT (ros2_shim): `rclcpp::Clock::ROS_TIME`, not `RCL_ROS_TIME`. They
    // compare equal because the port's `ClockType` puts ROS_TIME first, but
    // naming the rcl enum here mixes two enums and gcc says so at every build.
    const bool is_sim_time =
      clock_->get_clock_type() == rclcpp::Clock::ROS_TIME &&
      clock_->ros_time_is_active();
    if (is_sim_time) {
      // Sim time diverges from wall time — poll the target clock in short
      // intervals while remaining interruptible by emitWakeUpSignal().
      //
      // PORT (ros2_shim): the wait is bounded by wall time as well as by the
      // target clock, and that bound is what keeps the tree tick loop alive.
      // Simulated time here advances only where `/clock` is polled
      // (`rclcpp::Clock::set_sim_time`, fed by the node's `/clock`
      // subscription), and the loop this `sleep()` is called from -
      // `BehaviorTreeEngine::run` - is one of the two threads that can be
      // waiting for it. A clock that has not reached `next_interval` therefore
      // cannot get there while this loop spins: the wait would never see the
      // time it is waiting for. The tree then stops ticking in the middle of a
      // goal and the goal hangs until something calls `emitWakeUpSignal()` - a
      // cancel, or a `haltTree()`. That is the symptom this port reproduced:
      // `follow_waypoints` accepted, `current_waypoint` never advancing, no
      // `/cmd_vel` at all, and the goal ending CANCELED the instant the client
      // gave up - on roughly half of its waypoint runs. Bounding by wall time
      // degrades a starved clock to the wait the non-simulated branch makes
      // anyway, instead of to a hang.
      const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::nanoseconds((next_interval - clock_->now()).nanoseconds());
      while (clock_->now() < next_interval) {
        if (wake_up->waitFor(std::chrono::microseconds(500))) {
          return true;  // preempted
        }
        if (std::chrono::steady_clock::now() >= deadline) {
          break;  // the clock is not advancing; do not wait on it forever
        }
      }
    } else {
      // Steady/system clock agrees with wall time — sleep for the exact
      // remaining duration, still interruptible by emitWakeUpSignal().
      auto remaining = next_interval - clock_->now();
      wake_up->waitFor(
        std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::nanoseconds(remaining.nanoseconds())));
    }
    return true;
  }

  std::chrono::nanoseconds period() const
  {
    return std::chrono::nanoseconds(period_.nanoseconds());
  }

private:
  rclcpp::Clock::SharedPtr clock_;
  rclcpp::Duration period_;
  rclcpp::Time last_interval_;
  BT::Tree * tree_;
};

}  // namespace nav2_behavior_tree

#endif  // NAV2_BEHAVIOR_TREE__UTILS__LOOP_RATE_HPP_
