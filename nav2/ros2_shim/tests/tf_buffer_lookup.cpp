// Pins the shim's transform *history*: a lookup at a stamp hands back the pose
// that was broadcast for that stamp, even while the body keeps moving.
//
// The failure this exists to catch is silent. The mapping run drove a robot on
// a 1.5 m circle and both streams were complete and correct -- /tf carried all
// 48 odometry samples with the right stamps and the right translations, /scan
// carried all 48 scans -- yet every pose the lookup returned sat a third of the
// way to the origin (0.5 m instead of 1.5 m), rotated by 90 degrees. Nothing
// complained about the transform itself; what broke was three frames further
// downstream: slam_toolbox's minimum-travel gate compares consecutive poses, a
// true 0.196 m step arrived as 0.065 m, under its 0.1 m threshold, so every
// other scan was dropped, half the scans never reached the mapper, and the map
// came out the wrong shape (8.95 x 10.15 m instead of 8 x 6 m).
//
// A lookup has to answer three different questions correctly, and the test
// covers each:
//
//   1. exactly on a sample -- the pose that was broadcast, untouched; this is
//      the one slam_toolbox, AMCL and the costmap layers all ask,
//   2. between two samples -- a blend of them, never the neighbouring pose and
//      never a scaled one,
//   3. past the newest sample -- an error, which is what makes a scan that
//      arrived before its odometry get dropped instead of matched against the
//      pose that comes after it.
//
// It also keeps the rest of the tree out of the answer: slam_toolbox publishes
// `map -> odom` itself, stamped 0.2 s ahead of the scan it belongs to, and that
// must not leak into an `odom -> base_footprint` lookup.
//
// It runs without the shared-memory graph.

#include "geometry_msgs/msg/transform_stamped.hpp"
#include "tf2_ros/buffer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

bool close(double a, double b, double tolerance = 1e-9)
{
  return std::fabs(a - b) <= tolerance;
}

constexpr double kPi = 3.14159265358979323846;

// The same lap the mapping test drives: 1.5 m radius, 48 poses, ~235 ms apart.
constexpr int kSteps = 48;
constexpr double kRadius = 1.5;
constexpr double kStepSeconds = 0.235;
// slam_toolbox's `transform_timeout`, which is how far ahead of its scan the
// `map -> odom` it publishes is stamped.
constexpr double kTransformLeadSeconds = 0.2;

struct Pose
{
  double x = 0.0;
  double y = 0.0;
  double yaw = 0.0;
};

/// The stamps the run is driven at, in whole nanoseconds since the epoch, so a
/// test lookup can name an instant exactly.
std::vector<tf2::TimePoint> stamps()
{
  std::vector<tf2::TimePoint> out;
  const int64_t start = 1789399000000000000LL;
  for (int step = 0; step < kSteps; ++step) {
    out.emplace_back(tf2::Duration(
      start + static_cast<int64_t>(step * kStepSeconds * 1e9)));
  }
  return out;
}

Pose drivenPose(int step)
{
  const double theta = 2.0 * kPi * step / kSteps;
  return {kRadius * std::cos(theta), kRadius * std::sin(theta), theta + kPi / 2.0};
}

double yawOf(const geometry_msgs::msg::Quaternion & q)
{
  return std::atan2(
    2.0 * (q.w * q.z + q.x * q.y),
    1.0 - 2.0 * (q.y * q.y + q.z * q.z));
}

double normalize(double angle)
{
  while (angle > kPi) {
    angle -= 2.0 * kPi;
  }
  while (angle < -kPi) {
    angle += 2.0 * kPi;
  }
  return angle;
}

geometry_msgs::msg::TransformStamped transformOf(
  const std::string & parent, const std::string & child, const Pose & pose,
  const tf2::TimePoint & stamp)
{
  geometry_msgs::msg::TransformStamped msg;
  msg.header.frame_id = parent;
  msg.child_frame_id = child;
  msg.header.stamp = tf2::timePointToRosTime(stamp);
  msg.transform.translation.x = pose.x;
  msg.transform.translation.y = pose.y;
  msg.transform.rotation.z = std::sin(pose.yaw / 2.0);
  msg.transform.rotation.w = std::cos(pose.yaw / 2.0);
  return msg;
}

/// A transform carrying no displacement, stamped at `stamp`, which is what
/// `GetPoseHelper::getOdomPose` composes: asking for the pose of a frame is
/// looking up the transform of a zero transform expressed in that frame.
geometry_msgs::msg::TransformStamped poseQuery(
  const std::string & frame, const tf2::TimePoint & stamp)
{
  geometry_msgs::msg::TransformStamped msg;
  msg.header.frame_id = frame;
  msg.header.stamp = tf2::timePointToRosTime(stamp);
  msg.transform.rotation.w = 1.0;
  return msg;
}

}  // namespace

int main()
{
  const std::vector<tf2::TimePoint> time = stamps();

  tf2_ros::Buffer buffer(nullptr, tf2::durationFromSec(30.0));
  for (int step = 0; step < kSteps; ++step) {
    // The odometry a robot body publishes.
    buffer.setTransform(
      transformOf("odom", "base_footprint", drivenPose(step), time[step]), false);
    // What slam_toolbox broadcasts alongside it, deliberately in the future of
    // the scan it belongs to.
    Pose map_to_odom;
    map_to_odom.yaw = -drivenPose(step).yaw;
    buffer.setTransform(
      transformOf(
        "map", "odom", map_to_odom,
        time[step] + tf2::durationFromSec(kTransformLeadSeconds)),
      false);
  }

  // 1. Exactly on a sample: the broadcast pose, for every step of the lap.
  double worst_position = 0.0;
  double worst_yaw = 0.0;
  for (int step = 0; step < kSteps; ++step) {
    const Pose truth = drivenPose(step);
    const auto found = buffer.lookupTransform("odom", "base_footprint", time[step]);
    worst_position = std::max(
      worst_position,
      std::hypot(
        found.transform.translation.x - truth.x, found.transform.translation.y - truth.y));
    worst_yaw = std::max(
      worst_yaw, std::fabs(normalize(yawOf(found.transform.rotation) - truth.yaw)));
  }
  std::printf(
    "  worst lookup error over the lap: %.6f m / %.6f deg\n",
    worst_position, worst_yaw * 180.0 / kPi);
  expect(worst_position < 1e-9, "every stamped lookup returns the pose broadcast for it");
  expect(worst_yaw < 1e-9, "every stamped lookup returns the heading broadcast for it");

  // The same question through the call slam_toolbox's `getOdomPose` makes: a
  // zero transform in the frame, transformed into the odometry frame.
  {
    const Pose truth = drivenPose(7);
    const auto out = buffer.transform(poseQuery("base_footprint", time[7]), "odom");
    expect(
      close(out.transform.translation.x, truth.x, 1e-9) &&
      close(out.transform.translation.y, truth.y, 1e-9) &&
      close(normalize(yawOf(out.transform.rotation) - truth.yaw), 0.0, 1e-9),
      "the transform() overload slam_toolbox uses answers the same way");
  }

  // 2. The distance the minimum-travel gate sees, per step. A scaled pose still
  //    steps, just by the wrong amount, which is what made this invisible.
  {
    double smallest_step = 1e9;
    for (int step = 1; step < kSteps; ++step) {
      const auto before = buffer.lookupTransform("odom", "base_footprint", time[step - 1]);
      const auto after = buffer.lookupTransform("odom", "base_footprint", time[step]);
      smallest_step = std::min(
        smallest_step,
        std::hypot(
          after.transform.translation.x - before.transform.translation.x,
          after.transform.translation.y - before.transform.translation.y));
    }
    std::printf("  smallest step between consecutive lookups: %.4f m\n", smallest_step);
    expect(
      smallest_step > 0.19,
      "consecutive poses are a full step apart, above a 0.1 m travel gate");
  }

  // 3. Between two samples: the blend of the two, not the newer one and not a
  //    shrunken one.
  {
    const tf2::TimePoint middle = tf2::TimePoint(
      (time[3].time_since_epoch() + time[4].time_since_epoch()) / 2);
    const auto found = buffer.lookupTransform("odom", "base_footprint", middle);
    const Pose a = drivenPose(3);
    const Pose b = drivenPose(4);
    expect(
      close(found.transform.translation.x, (a.x + b.x) / 2.0, 1e-9) &&
      close(found.transform.translation.y, (a.y + b.y) / 2.0, 1e-9),
      "a lookup between two samples lands on the straight line between them");
  }

  // 4. Past the newest sample: an error, so a scan whose odometry has not
  //    arrived is dropped rather than matched against the next pose.
  {
    bool threw = false;
    try {
      buffer.lookupTransform(
        "odom", "base_footprint",
        time[kSteps - 1] + tf2::Duration(1));
    } catch (const tf2::TransformException &) {
      threw = true;
    }
    expect(threw, "a lookup past the newest sample is an extrapolation error");

    bool knows = true;
    try {
      buffer.lookupTransform("odom", "base_footprint", time[kSteps - 1]);
    } catch (const tf2::TransformException &) {
      knows = false;
    }
    expect(knows, "the newest sample itself is still answerable");
  }

  // 5. `map -> odom`, published ahead of everything else, stays out of the
  //    answer: a lookup of `odom -> base_footprint` walks no higher than odom.
  {
    tf2_ros::Buffer without(nullptr, tf2::durationFromSec(30.0));
    for (int step = 0; step < kSteps; ++step) {
      without.setTransform(
        transformOf("odom", "base_footprint", drivenPose(step), time[step]), false);
    }
    double worst = 0.0;
    for (int step = 0; step < kSteps; ++step) {
      const auto with_map = buffer.lookupTransform("odom", "base_footprint", time[step]);
      const auto bare = without.lookupTransform("odom", "base_footprint", time[step]);
      worst = std::max(
        worst,
        std::hypot(
          with_map.transform.translation.x - bare.transform.translation.x,
          with_map.transform.translation.y - bare.transform.translation.y));
    }
    expect(worst < 1e-12, "a map frame published around the answer does not change it");
  }

  // 6. A frame that only ever gets a zero stamp is a fixed relation: it answers
  //    any instant, which is how `/tf_static` reaches a consumer.
  {
    Pose laser;
    laser.x = 0.2;
    buffer.setTransform(transformOf("base_footprint", "laser", laser, tf2::TimePointZero), true);
    const tf2::TimePoint any = time[11] + tf2::durationFromSec(3.3);
    const auto found = buffer.lookupTransform("laser", "base_footprint", any);
    // The relation is broadcast as `base_footprint -> laser`, so asking for the
    // pose of `base_footprint` in the laser frame is the inverse: -0.2 m.
    char described[200];
    std::snprintf(
      described, sizeof(described),
      "a static transform is answered at an instant that was never broadcast (x=%.9f)",
      found.transform.translation.x);
    expect(
      close(found.transform.translation.x, -0.2, 1e-9),
      described);
  }

  if (failures == 0) {
    std::printf("  tf buffer lookup: all checks passed\n");
  }
  return failures == 0 ? 0 : 1;
}
