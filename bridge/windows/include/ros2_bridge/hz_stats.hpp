#ifndef ROS2_BRIDGE_HZ_STATS_HPP
#define ROS2_BRIDGE_HZ_STATS_HPP

// The numbers `ros2 topic hz` prints.
//
// The command used to print `std dev: 0.0` for every window: the intervals
// were collected, the average and the extremes were computed, and the standard
// deviation was written as a literal zero. A user watching a jittery
// publisher was told the period was perfectly regular.
//
// The computation lives here rather than in the command so that it can be
// tested: `bridge/windows/tests/hz_stats.cpp` pins down the three cases that
// matter - a fixed period, a jittered period and a period that recovers after
// a pause - and the CLI test then only has to check that the printed line
// carries the number.
//
// The standard deviation is the *population* one (the divisor is N, not N-1),
// which is what `ros2 topic hz` in ROS2 reports: its window is the whole
// history it keeps, not a sample drawn from a larger population.
//
// Empty input is not an error and not a division by zero: there is no window
// to describe yet, so `average` stays zero and `valid` is false.

#include <cmath>
#include <cstddef>
#include <vector>

namespace ros2_bridge
{

struct HzStats
{
  // The average rate, in messages per second. Zero when there is no window.
  double average_rate = 0.0;
  // The fastest and the slowest rate in the window, in messages per second.
  double min_rate = 0.0;
  double max_rate = 0.0;
  // The standard deviation of the *intervals*, in seconds. This is what
  // `ros2 topic hz` prints: it describes the spread of the message period,
  // not the spread of the rate.
  double std_dev = 0.0;
  // The average interval, in seconds.
  double average_interval = 0.0;
  // How many intervals the window holds.
  std::size_t count = 0;
  // False while there is nothing to report.
  bool valid = false;
};

// The statistics of a window of message intervals, in seconds.
//
// An interval of zero (two messages that arrived in the same clock tick)
// would make the rate infinite, so it is skipped: it carries no information
// about the period and it would poison both the average and the standard
// deviation.
inline HzStats computeHzStats(const std::vector<double> & intervals)
{
  HzStats stats;
  std::vector<double> usable;
  usable.reserve(intervals.size());
  for (double value : intervals) {
    if (value > 0.0 && std::isfinite(value)) {
      usable.push_back(value);
    }
  }
  if (usable.empty()) {
    return stats;
  }

  double sum = 0.0;
  double minimum = usable.front();
  double maximum = usable.front();
  for (double value : usable) {
    sum += value;
    if (value < minimum) {
      minimum = value;
    }
    if (value > maximum) {
      maximum = value;
    }
  }
  const double count = static_cast<double>(usable.size());
  const double average = sum / count;

  double variance = 0.0;
  for (double value : usable) {
    const double delta = value - average;
    variance += delta * delta;
  }
  variance /= count;

  stats.average_interval = average;
  stats.average_rate = 1.0 / average;
  // The fastest rate comes from the shortest interval and the slowest rate
  // from the longest one, which is the order the two are swapped in here.
  stats.min_rate = 1.0 / maximum;
  stats.max_rate = 1.0 / minimum;
  stats.std_dev = std::sqrt(variance);
  stats.count = usable.size();
  stats.valid = true;
  return stats;
}

}  // namespace ros2_bridge

#endif  // ROS2_BRIDGE_HZ_STATS_HPP
