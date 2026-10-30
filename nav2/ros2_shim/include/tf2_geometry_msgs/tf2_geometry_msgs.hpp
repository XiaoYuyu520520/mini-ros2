#ifndef TF2_GEOMETRY_MSGS__TF2_GEOMETRY_MSGS_HPP_
#define TF2_GEOMETRY_MSGS__TF2_GEOMETRY_MSGS_HPP_

#include <cstddef>

#include "geometry_msgs/msg/point.hpp"
#include "geometry_msgs/msg/point32.hpp"
#include "geometry_msgs/msg/point_stamped.hpp"
#include "geometry_msgs/msg/polygon.hpp"
#include "geometry_msgs/msg/polygon_stamped.hpp"
#include "geometry_msgs/msg/pose.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "geometry_msgs/msg/pose_with_covariance_stamped.hpp"
#include "geometry_msgs/msg/quaternion.hpp"
#include "geometry_msgs/msg/quaternion_stamped.hpp"
#include "geometry_msgs/msg/transform.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "geometry_msgs/msg/vector3.hpp"
#include "geometry_msgs/msg/vector3_stamped.hpp"
#include "tf2/convert.hpp"
#include "tf2/transform_datatypes.hpp"
#include "tf2/LinearMath/Matrix3x3.hpp"
#include "tf2/LinearMath/Quaternion.hpp"
#include "tf2/LinearMath/Transform.hpp"
#include "tf2/LinearMath/Vector3.hpp"

namespace tf2
{

// ---- Vector3 --------------------------------------------------------------
inline void fromMsg(const geometry_msgs::msg::Vector3 & in, tf2::Vector3 & out)
{
  out = tf2::Vector3(in.x, in.y, in.z);
}

inline geometry_msgs::msg::Vector3 toMsg(const tf2::Vector3 & in)
{
  geometry_msgs::msg::Vector3 out;
  out.x = in.x();
  out.y = in.y();
  out.z = in.z();
  return out;
}

// ---- Point ----------------------------------------------------------------
inline void fromMsg(const geometry_msgs::msg::Point & in, tf2::Vector3 & out)
{
  out = tf2::Vector3(in.x, in.y, in.z);
}

inline void fromMsg(const geometry_msgs::msg::Point32 & in, tf2::Vector3 & out)
{
  out = tf2::Vector3(in.x, in.y, in.z);
}

// ---- Quaternion -----------------------------------------------------------
inline void fromMsg(const geometry_msgs::msg::Quaternion & in, tf2::Quaternion & out)
{
  out = tf2::Quaternion(in.x, in.y, in.z, in.w);
}

inline geometry_msgs::msg::Quaternion toMsg(const tf2::Quaternion & in)
{
  geometry_msgs::msg::Quaternion out;
  out.x = in.x();
  out.y = in.y();
  out.z = in.z();
  out.w = in.w();
  return out;
}

// ---- Transform ------------------------------------------------------------
inline void fromMsg(const geometry_msgs::msg::Transform & in, tf2::Transform & out)
{
  tf2::Quaternion q;
  fromMsg(in.rotation, q);
  tf2::Vector3 v;
  fromMsg(in.translation, v);
  out = tf2::Transform(q, v);
}

inline geometry_msgs::msg::Transform toMsg(const tf2::Transform & in)
{
  geometry_msgs::msg::Transform out;
  out.translation = toMsg(in.getOrigin());
  out.rotation = toMsg(in.getRotation());
  return out;
}

inline void toMsg(const tf2::Transform & in, geometry_msgs::msg::Transform & out)
{
  out = toMsg(in);
}

// ---- Pose -----------------------------------------------------------------
inline void fromMsg(const geometry_msgs::msg::Pose & in, tf2::Transform & out)
{
  tf2::Quaternion q;
  fromMsg(in.orientation, q);
  tf2::Vector3 v;
  fromMsg(in.position, v);
  out = tf2::Transform(q, v);
}

inline geometry_msgs::msg::Pose toMsgPose(const tf2::Transform & in)
{
  geometry_msgs::msg::Pose out;
  const tf2::Vector3 origin = in.getOrigin();
  const tf2::Quaternion rotation = in.getRotation();
  out.position.x = origin.x();
  out.position.y = origin.y();
  out.position.z = origin.z();
  out.orientation.x = rotation.x();
  out.orientation.y = rotation.y();
  out.orientation.z = rotation.z();
  out.orientation.w = rotation.w();
  return out;
}

inline void toMsg(const tf2::Transform & in, geometry_msgs::msg::Pose & out)
{
  out = toMsgPose(in);
}

// ---- stamped conversions --------------------------------------------------
inline void fromMsg(const geometry_msgs::msg::PoseStamped & in, tf2::Stamped<tf2::Transform> & out)
{
  out.stamp_ = timePointFromRosTime(in.header.stamp);
  out.frame_id_ = in.header.frame_id;
  tf2::Transform t;
  fromMsg(in.pose, t);
  out.setData(t);
}

inline void fromMsg(
  const geometry_msgs::msg::TransformStamped & in, tf2::Stamped<tf2::Transform> & out)
{
  out.stamp_ = timePointFromRosTime(in.header.stamp);
  out.frame_id_ = in.header.frame_id;
  tf2::Transform t;
  fromMsg(in.transform, t);
  out.setData(t);
}

// A stamped transform becomes a `TransformStamped`, not a `PoseStamped`: that
// is what upstream returns for this argument type, and the difference is not
// cosmetic - `nav2_collision_monitor`'s `PolygonSource` feeds the result
// straight back into `tf2::doTransform`, which wants the transform and not the
// pose it corresponds to.
inline geometry_msgs::msg::TransformStamped toMsg(const tf2::Stamped<tf2::Transform> & in)
{
  geometry_msgs::msg::TransformStamped out;
  out.header.stamp = timePointToRosTime(in.stamp_);
  out.header.frame_id = in.frame_id_;
  out.child_frame_id = "";
  out.transform = toMsg(static_cast<const tf2::Transform &>(in));
  return out;
}

// The `PoseStamped` spelling is kept under its own name rather than overloaded
// on the return type, which C++ does not allow. `toMsgPose` above covers the
// bare `tf2::Transform`; this is the stamped one.
inline geometry_msgs::msg::PoseStamped toMsgPoseStamped(const tf2::Stamped<tf2::Transform> & in)
{
  geometry_msgs::msg::PoseStamped out;
  out.header.stamp = timePointToRosTime(in.stamp_);
  out.header.frame_id = in.frame_id_;
  out.pose = toMsgPose(static_cast<const tf2::Transform &>(in));
  return out;
}

// ---- doTransform ----------------------------------------------------------
inline void doTransform(
  const geometry_msgs::msg::PoseStamped & in, geometry_msgs::msg::PoseStamped & out,
  const geometry_msgs::msg::TransformStamped & transform)
{
  tf2::Transform tr;
  fromMsg(transform.transform, tr);
  tf2::Transform in_tf;
  fromMsg(in.pose, in_tf);
  out = in;
  out.header.frame_id = transform.header.frame_id;
  out.pose = toMsgPose(tr * in_tf);
}

inline void doTransform(
  const geometry_msgs::msg::PointStamped & in, geometry_msgs::msg::PointStamped & out,
  const geometry_msgs::msg::TransformStamped & transform)
{
  tf2::Transform tr;
  fromMsg(transform.transform, tr);
  tf2::Vector3 v;
  fromMsg(in.point, v);
  const tf2::Vector3 result = tr(v);
  out = in;
  out.header.frame_id = transform.header.frame_id;
  out.point.x = result.x();
  out.point.y = result.y();
  out.point.z = result.z();
}

// A polygon is a list of points, and each point goes through the same
// rotate-then-translate as a PointStamped. `nav2_collision_monitor`'s
// `PolygonSource` is the caller: it takes a `PolygonInstanceStamped` off a
// topic and needs its vertices in the base frame before it can sample them.
inline void doTransform(
  const geometry_msgs::msg::PolygonStamped & in, geometry_msgs::msg::PolygonStamped & out,
  const geometry_msgs::msg::TransformStamped & transform)
{
  tf2::Transform tr;
  fromMsg(transform.transform, tr);
  out = in;
  out.header.frame_id = transform.header.frame_id;
  out.polygon.points.resize(in.polygon.points.size());
  for (std::size_t i = 0; i < in.polygon.points.size(); ++i) {
    tf2::Vector3 v;
    fromMsg(in.polygon.points[i], v);
    const tf2::Vector3 result = tr(v);
    out.polygon.points[i].x = static_cast<float>(result.x());
    out.polygon.points[i].y = static_cast<float>(result.y());
    out.polygon.points[i].z = static_cast<float>(result.z());
  }
}

inline void doTransform(
  const geometry_msgs::msg::TransformStamped & in, geometry_msgs::msg::TransformStamped & out,
  const geometry_msgs::msg::TransformStamped & transform)
{
  tf2::Transform tr;
  fromMsg(transform.transform, tr);
  tf2::Transform in_tf;
  fromMsg(in.transform, in_tf);
  out = in;
  out.header.frame_id = transform.header.frame_id;
  out.transform = toMsg(tr * in_tf);
}

// Rotations carry over unchanged: only the frame is rewritten.
inline void doTransform(
  const geometry_msgs::msg::QuaternionStamped & in,
  geometry_msgs::msg::QuaternionStamped & out,
  const geometry_msgs::msg::TransformStamped & transform)
{
  tf2::Quaternion rotation;
  fromMsg(transform.transform.rotation, rotation);
  tf2::Quaternion in_rotation;
  fromMsg(in.quaternion, in_rotation);
  out = in;
  out.header.frame_id = transform.header.frame_id;
  out.quaternion = toMsg(rotation * in_rotation);
}

// Vectors are rotated but not translated.
inline void doTransform(
  const geometry_msgs::msg::Vector3Stamped & in, geometry_msgs::msg::Vector3Stamped & out,
  const geometry_msgs::msg::TransformStamped & transform)
{
  tf2::Transform tr;
  fromMsg(transform.transform, tr);
  tf2::Vector3 v;
  fromMsg(in.vector, v);
  const tf2::Vector3 rotated = tr.getRotation() * v;
  out = in;
  out.header.frame_id = transform.header.frame_id;
  out.vector = toMsg(rotated);
}

}  // namespace tf2

#endif  // TF2_GEOMETRY_MSGS__TF2_GEOMETRY_MSGS_HPP_
