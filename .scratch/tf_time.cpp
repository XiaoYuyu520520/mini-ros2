// The shim's transform tree is time-aware.
//
// Every consumer that pairs a message with a pose -- slam_toolbox's
// `getOdomPose`, AMCL, the costmap layers -- looks the transform up *at the
// instant stamped on the message*, so the buffer has to answer for that
// instant rather than for whatever was broadcast last. This checks the four
// properties that matter:
//
//   1. a lookup between two samples interpolates,
//   2. a lookup at the newest sample is exact, and TimePointZero means "newest",
//   3. a static frame answers at any instant,
//   4. only the links below the deepest shared ancestor are consulted, so a
//      `map -> odom` published a few milliseconds behind the scan does not turn
//      an `odom -> base_footprint` lookup into an extrapolation.
//
// It runs without the shared-memory graph: the buffer is fed directly.

#include "tf2/buffer_core.hpp"
#include "tf2/time.hpp"
#include "tf2_ros/buffer.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{

int failures = 0;

void expect(bool condition, const std::string & what)
{
  std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
  if (!condition) {
    ++failures;
  }
}

tf2::TimePoint at(double seconds)
{
  return tf2::TimePoint(std::chrono::nanoseconds(static_cast<int64_t>(seconds * 1e9)));
}

geometry_msgs::msg::TransformStamped make_transform(
  const std::string & parent, const std::string & child, double x, const tf2::TimePoint & stamp)
{
  geometry_msgs::msg::TransformStamped msg;
  msg.header.frame_id = parent;
  msg.child_frame_id = child;
  msg.header.stamp = tf2::timePointToRosTime(stamp);
  msg.transform.translation.x = x;
  msg.transform.rotation.w = 1.0;
  return msg;
}

double x_of(const geometry_msgs::msg::TransformStamped & msg)
{
  return msg.transform.translation.x;
}

bool close(double a, double b)
{
  return std::fabs(a - b) < 1e-9;
}

bool extrapolated(const std::function<void()> & lookup)
{
  try {
    lookup();
  } catch (const tf2::ExtrapolationException &) {
    return true;
  } catch (const tf2::TransformException &) {
    return false;
  }
  return false;
}

}  // namespace

int main()
{
  tf2::BufferCore core;
  core.setTransform(make_transform("odom", "base", 1.0, at(1.0)));
  core.setTransform(make_transform("odom", "base", 3.0, at(3.0)));

  expect(
    close(x_of(core.lookupTransform("odom", "base", at(2.0))), 2.0),
    "a lookup between two samples interpolates");
  expect(
    close(x_of(core.lookupTransform("odom", "base", at(1.5))), 1.5),
    "interpolation is linear in time");
  expect(
    close(x_of(core.lookupTransform("odom", "base", at(3.0))), 3.0),
    "a lookup on a sample returns it exactly");
  expect(
    close(x_of(core.lookupTransform("odom", "base", tf2::TimePointZero)), 3.0),
    "TimePointZero means the newest sample");
  expect(close(x_of(core.lookupTransform("odom", "base")), 3.0), "the untimed form too");
  expect(
    core.canTransform("odom", "base", at(2.0)), "canTransform answers for an interpolated instant");
  expect(
    extrapolated([&core]() {core.lookupTransform("odom", "base", at(4.0));}),
    "a lookup past the newest sample is an extrapolation");
  expect(
    !core.canTransform("odom", "base", at(4.0)), "and canTransform says so instead of throwing");

  // Static transforms describe one fixed relation, so they answer at any time.
  core.setTransform(make_transform("base", "laser", 0.25, at(1.0)), true);
  expect(
    close(x_of(core.lookupTransform("base", "laser", at(9.0))), 0.25),
    "a static frame answers long after it was broadcast");
  expect(
    close(x_of(core.lookupTransform("odom", "laser", at(2.0))), 2.25),
    "and composes with the driven ones");

  // The chain is only followed up to the deepest frame both sides share.
  tf2::BufferCore nested;
  nested.setTransform(make_transform("map", "odom", 10.0, at(1.0)));
  nested.setTransform(make_transform("odom", "base", 2.0, at(5.0)));
  expect(
    close(x_of(nested.lookupTransform("odom", "base", at(5.0))), 2.0),
    "a lookup does not need the frames above its target");
  nested.setTransform(make_transform("map", "odom", 10.0, at(5.0)));
  expect(
    close(x_of(nested.lookupTransform("map", "base", at(5.0))), 12.0),
    "the whole chain still composes");

  // `tf2_ros::Buffer::transform` reads the stamp off the message it is given.
  tf2_ros::Buffer buffer;
  buffer.setTransform(make_transform("odom", "base", 1.0, at(1.0)));
  buffer.setTransform(make_transform("odom", "base", 3.0, at(3.0)));
  geometry_msgs::msg::TransformStamped identity;
  identity.header.frame_id = "base";
  identity.header.stamp = tf2::timePointToRosTime(at(2.0));
  identity.transform.rotation.w = 1.0;
  const geometry_msgs::msg::TransformStamped moved = buffer.transform(identity, "odom");
  expect(
    close(x_of(moved), 2.0),
    "transform() uses the stamp carried by the message");

  if (failures != 0) {
    std::printf("tf_time: %d check(s) failed\n", failures);
    return 1;
  }
  std::printf("tf_time: all checks passed\n");
  return 0;
}
