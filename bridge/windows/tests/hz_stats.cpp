// bridge_hz_stats - the numbers behind `ros2 topic hz`.
//
// The command used to print `std dev: 0.0` no matter how irregular the
// publisher was: the intervals were collected and then the standard deviation
// was written as a literal zero. These cases pin down the fix. They cover the
// three periods a user actually sees - a steady publisher, a jittery one, and
// one that stalls and then recovers - plus the empty window that must not be
// a division by zero.

#include <cmath>
#include <cstdio>
#include <vector>

#include "ros2_bridge/hz_stats.hpp"

namespace
{

int g_failures = 0;
int g_checks = 0;

void expect(bool condition, const char * what)
{
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::fprintf(stderr, "  FAIL %s\n", what);
  }
}

void expectNear(double actual, double expected, double tolerance, const char * what)
{
  ++g_checks;
  if (std::fabs(actual - expected) > tolerance) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: actual %.9f, expected %.9f (+/- %.9f)\n",
      what, actual, expected, tolerance);
  }
}

// A ten hertz publisher: the interval is 0.1 s every time.
void testFixedPeriod()
{
  const std::vector<double> intervals(5, 0.1);
  const ros2_bridge::HzStats stats = ros2_bridge::computeHzStats(intervals);
  expect(stats.valid, "fixed: window is valid");
  expect(stats.count == 5, "fixed: count");
  expectNear(stats.average_rate, 10.0, 1e-9, "fixed: average rate");
  expectNear(stats.min_rate, 10.0, 1e-9, "fixed: min rate");
  expectNear(stats.max_rate, 10.0, 1e-9, "fixed: max rate");
  // The whole point of the bug: a perfectly regular publisher is zero.
  expectNear(stats.std_dev, 0.0, 1e-12, "fixed: std dev is zero");
}

// A jittery publisher: 0.09 / 0.11 alternating around a 0.1 s mean. The
// population standard deviation of the two values is exactly 0.01 s.
void testJitteredPeriod()
{
  const std::vector<double> intervals{0.09, 0.11, 0.09, 0.11};
  const ros2_bridge::HzStats stats = ros2_bridge::computeHzStats(intervals);
  expect(stats.valid, "jitter: window is valid");
  expect(stats.count == 4, "jitter: count");
  expectNear(stats.average_interval, 0.1, 1e-9, "jitter: average interval");
  expectNear(stats.average_rate, 10.0, 1e-9, "jitter: average rate");
  expectNear(stats.std_dev, 0.01, 1e-9, "jitter: std dev is not zero");
  // The shortest interval is the fastest rate, the longest the slowest.
  expectNear(stats.max_rate, 1.0 / 0.09, 1e-9, "jitter: max rate");
  expectNear(stats.min_rate, 1.0 / 0.11, 1e-9, "jitter: min rate");
}

// A publisher that stalls for a second and then resumes: the spread must be
// large and the extremes must land on the right end of the window.
void testPauseAndRecover()
{
  const std::vector<double> intervals{0.1, 0.1, 1.1, 0.1, 0.1};
  const ros2_bridge::HzStats stats = ros2_bridge::computeHzStats(intervals);
  expect(stats.valid, "pause: window is valid");
  expect(stats.count == 5, "pause: count");
  // A little over 0.3 s of mean, since one interval is eleven times the rest.
  expectNear(stats.average_interval, (0.1 + 0.1 + 1.1 + 0.1 + 0.1) / 5.0, 1e-9,
    "pause: average interval");
  expectNear(stats.min_rate, 1.0 / 1.1, 1e-9, "pause: slowest rate is the stall");
  expectNear(stats.max_rate, 10.0, 1e-9, "pause: fastest rate");
  // The deviation has to be well above the mean period itself.
  expect(stats.std_dev > 0.3, "pause: std dev reflects the spread");
}

// An empty window is not a division by zero; the caller prints nothing.
void testEmpty()
{
  const ros2_bridge::HzStats stats = ros2_bridge::computeHzStats({});
  expect(!stats.valid, "empty: not valid");
  expect(stats.count == 0, "empty: count");
  expect(stats.std_dev == 0.0, "empty: std dev");
}

// A zero interval (two messages in the same clock tick) carries no period;
// it is skipped rather than turned into an infinite rate.
void testZeroIntervalSkipped()
{
  const std::vector<double> intervals{0.0, 0.1, 0.1};
  const ros2_bridge::HzStats stats = ros2_bridge::computeHzStats(intervals);
  expect(stats.valid, "zero: window is valid");
  expect(stats.count == 2, "zero: zero interval skipped");
  expectNear(stats.average_rate, 10.0, 1e-9, "zero: average rate");
}

}  // namespace

int main()
{
  testFixedPeriod();
  testJitteredPeriod();
  testPauseAndRecover();
  testEmpty();
  testZeroIntervalSkipped();

  if (g_failures == 0) {
    std::printf("bridge_hz_stats: %d checks passed\n", g_checks);
    return 0;
  }
  std::fprintf(stderr, "bridge_hz_stats: %d/%d checks failed\n", g_failures, g_checks);
  return 1;
}
