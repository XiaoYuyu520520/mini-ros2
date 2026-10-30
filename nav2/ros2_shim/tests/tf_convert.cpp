// `tf2::convert` is the shim's single conversion funnel: the costmap turns its
// sensor origin into a pose through it, its layers turn a `Pose` into a tf2
// transform through it, the behavior tree turns a quaternion into a tf2
// quaternion through it. Two properties matter, and neither is visible at the
// call site:
//
//   1. two values of the *same* type copy. Upstream tf2 gives that case its own
//      overload; a conversion that quietly does nothing is not an error
//      anywhere -- the costmap only notices as "Sensor origin at (0.00, 0.00
//      0.00) is out of map bounds" and stops raytracing,
//   2. a message and its tf2 counterpart go through the concrete
//      `fromMsg`/`toMsg` pair of tf2_geometry_msgs rather than by assignment.
//
// It runs without the shared-memory graph.

#include "tf2/convert.hpp"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"

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

bool close(double a, double b)
{
  return std::fabs(a - b) < 1e-9;
}

}  // namespace

int main()
{
  // 1. same type: a copy, never a no-op.
  {
    geometry_msgs::msg::Point source;
    source.x = 1.0;
    source.y = 2.0;
    source.z = 3.0;
    geometry_msgs::msg::Point destination;
    tf2::convert(source, destination);
    expect(
      close(destination.x, 1.0) && close(destination.y, 2.0) && close(destination.z, 3.0),
      "a point is copied onto another point (the costmap's sensor origin)");
  }
  {
    geometry_msgs::msg::Pose source;
    source.position.x = 4.0;
    source.orientation.w = 1.0;
    geometry_msgs::msg::Pose destination;
    tf2::convert(source, destination);
    expect(
      close(destination.position.x, 4.0) && close(destination.orientation.w, 1.0),
      "a pose is copied onto another pose");
  }
  {
    tf2::Vector3 source(1.0, 2.0, 3.0);
    tf2::Vector3 destination;
    tf2::convert(source, destination);
    expect(
      close(destination.x(), 1.0) && close(destination.y(), 2.0) && close(destination.z(), 3.0),
      "a tf2 vector is copied onto another tf2 vector");
  }
  {
    geometry_msgs::msg::Point point;
    point.x = 7.0;
    tf2::convert(point, point);
    expect(close(point.x, 7.0), "converting a value onto itself leaves it alone");
  }

  // 2. message <-> tf2: the concrete overloads, not assignment.
  {
    geometry_msgs::msg::Point source;
    source.x = 1.0;
    source.y = 2.0;
    source.z = 3.0;
    tf2::Vector3 destination;
    tf2::convert(source, destination);
    expect(
      close(destination.x(), 1.0) && close(destination.y(), 2.0) && close(destination.z(), 3.0),
      "a point becomes a tf2 vector");
  }
  {
    tf2::Vector3 source(1.0, 2.0, 3.0);
    geometry_msgs::msg::Vector3 destination;
    tf2::convert(source, destination);
    expect(
      close(destination.x, 1.0) && close(destination.y, 2.0) && close(destination.z, 3.0),
      "a tf2 vector becomes a vector3 message");
  }
  {
    geometry_msgs::msg::Quaternion source;
    source.z = 0.6;
    source.w = 0.8;
    tf2::Quaternion destination;
    tf2::convert(source, destination);
    expect(
      close(destination.z(), 0.6) && close(destination.w(), 0.8),
      "a quaternion message becomes a tf2 quaternion");
  }

  // Transform, and the stamped forms the listeners hand out.
  {
    geometry_msgs::msg::Transform source;
    source.translation.x = 5.0;
    source.rotation.w = 1.0;
    tf2::Transform destination;
    tf2::convert(source, destination);
    expect(close(destination.getOrigin().x(), 5.0), "a transform message becomes a tf2 transform");
    geometry_msgs::msg::Transform back;
    tf2::convert(destination, back);
    expect(close(back.translation.x, 5.0), "and back again");
  }
  {
    geometry_msgs::msg::PoseStamped source;
    source.header.frame_id = "map";
    source.pose.position.x = 2.0;
    source.pose.orientation.w = 1.0;
    tf2::Stamped<tf2::Transform> destination;
    tf2::convert(source, destination);
    expect(
      destination.frame_id_ == "map" && close(destination.getOrigin().x(), 2.0),
      "a pose stamped becomes a stamped tf2 transform, frame and all");
  }
  {
    geometry_msgs::msg::TransformStamped source;
    source.header.frame_id = "odom";
    source.child_frame_id = "base_link";
    source.transform.translation.x = 3.0;
    source.transform.rotation.w = 1.0;
    tf2::Stamped<tf2::Transform> destination;
    tf2::convert(source, destination);
    expect(
      destination.frame_id_ == "odom" && close(destination.getOrigin().x(), 3.0),
      "a transform stamped becomes a stamped tf2 transform");
  }

  if (failures != 0) {
    std::printf("tf_convert: %d check(s) failed\n", failures);
    return 1;
  }
  std::printf("tf_convert: all checks passed\n");
  return 0;
}
