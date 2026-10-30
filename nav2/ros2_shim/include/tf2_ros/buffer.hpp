#ifndef TF2_ROS__BUFFER_HPP_
#define TF2_ROS__BUFFER_HPP_

#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "geometry_msgs/msg/point_stamped.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "rclcpp/clock.hpp"
#include "rclcpp/time.hpp"
#include "tf2/buffer_core.hpp"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "tf2_sensor_msgs/tf2_sensor_msgs.hpp"
#include "tf2_ros/buffer_interface.hpp"
#include "tf2_ros/create_timer_ros.hpp"

namespace tf2_ros
{

namespace detail
{
template<class T, class = void>
struct has_pose : std::false_type {};

template<class T>
struct has_pose<T, std::void_t<decltype(std::declval<T>().pose)>> : std::true_type {};

template<class T, class = void>
struct has_point : std::false_type {};

template<class T>
struct has_point<T, std::void_t<decltype(std::declval<T>().point)>> : std::true_type {};

template<class T, class = void>
struct has_stamp : std::false_type {};

template<class T>
struct has_stamp<T, std::void_t<decltype(std::declval<T>().header.stamp)>> : std::true_type {};

template<class T>
inline constexpr bool has_pose_v = has_pose<T>::value;

template<class T>
inline constexpr bool has_point_v = has_point<T>::value;

/// `rclcpp::Time` carries nanoseconds since the epoch, which is exactly what
/// `tf2::TimePoint` is; this is the one place that conversion is spelled out.
inline tf2::TimePoint toTimePoint(const rclcpp::Time & time)
{
  return tf2::TimePoint(std::chrono::nanoseconds(time.nanoseconds()));
}
}  // namespace detail

class Buffer : public BufferInterface, public tf2::BufferCore
{
public:
  using SharedPtr = std::shared_ptr<Buffer>;

  explicit Buffer(rclcpp::Clock::SharedPtr clock = nullptr)
  : clock_(std::move(clock))
  {
  }

  // tf2_ros::Buffer(clock, cache_time) -- `cache_time` is how far back a
  // lookup may reach, exactly as upstream.
  Buffer(rclcpp::Clock::SharedPtr clock, tf2::Duration cache_time)
  : tf2::BufferCore(cache_time), clock_(std::move(clock))
  {
  }

  void setCreateTimerInterface(std::shared_ptr<CreateTimerInterface> timer)
  {
    timer_interface_ = std::move(timer);
  }

  void setUsingDedicatedThread(bool) {}

  bool canTransform(const std::string & target, const std::string & source)
  {
    return tf2::BufferCore::canTransform(target, source);
  }

  bool canTransform(
    const std::string & target, const std::string & source, const tf2::TimePoint & time)
  {
    return tf2::BufferCore::canTransform(target, source, time);
  }

  bool canTransform(
    const std::string & target, const std::string & source,
    const builtin_interfaces::msg::Time & time)
  {
    return tf2::BufferCore::canTransform(target, source, tf2::timePointFromRosTime(time));
  }

  bool canTransform(
    const std::string & target, const std::string & source, const rclcpp::Time & time)
  {
    return tf2::BufferCore::canTransform(target, source, detail::toTimePoint(time));
  }

  bool canTransform(
    const std::string & target, const std::string & source, const tf2::TimePoint & time,
    std::string * error_msg = nullptr)
  {
    (void)error_msg;
    return tf2::BufferCore::canTransform(target, source, time);
  }

  bool canTransform(
    const std::string & target, const std::string & source, const tf2::TimePoint & time,
    const tf2::Duration & timeout, std::string * error_msg = nullptr)
  {
    (void)error_msg;
    return tf2::BufferCore::canTransform(target, source, time, timeout);
  }

  // The `canTransform` side of the same pair, for the same reason: this is the
  // overload upstream's dock plugins call, with a detection stamp and an
  // `rclcpp::Duration::from_seconds(0.2)` budget for the transform to arrive.
  bool canTransform(
    const std::string & target, const std::string & source,
    const rclcpp::Time & time,
    const rclcpp::Duration & timeout = rclcpp::Duration::from_nanoseconds(0),
    std::string * error_msg = nullptr)
  {
    (void)error_msg;
    return tf2::BufferCore::canTransform(
      target, source, detail::toTimePoint(time),
      tf2::Duration(timeout.nanoseconds()));
  }

  geometry_msgs::msg::TransformStamped lookupTransform(
    const std::string & target, const std::string & source)
  {
    return tf2::BufferCore::lookupTransform(target, source);
  }

  geometry_msgs::msg::TransformStamped lookupTransform(
    const std::string & target, const std::string & source, const tf2::TimePoint & time)
  {
    return tf2::BufferCore::lookupTransform(target, source, time);
  }

  geometry_msgs::msg::TransformStamped lookupTransform(
    const std::string & target, const std::string & source, const rclcpp::Time & time)
  {
    return tf2::BufferCore::lookupTransform(target, source, detail::toTimePoint(time));
  }

  geometry_msgs::msg::TransformStamped lookupTransform(
    const std::string & target, const std::string & source,
    const builtin_interfaces::msg::Time & time)
  {
    return tf2::BufferCore::lookupTransform(target, source, tf2::timePointFromRosTime(time));
  }

  geometry_msgs::msg::TransformStamped lookupTransform(
    const std::string & target, const std::string & source,
    const tf2::TimePoint & time, const tf2::Duration & timeout)
  {
    return tf2::BufferCore::lookupTransform(target, source, time, timeout);
  }

  // The other half of the pair upstream declares: the same lookup in
  // `rclcpp`'s own types. It is not redundant. `tf2::BufferCore`'s overload
  // takes a `tf2::TimePoint` and a `tf2::Duration`, and a message stamp is
  // neither - `nav_msgs/Path::header.stamp` is a
  // `builtin_interfaces::msg::Time`, and `tf2::durationFromSec()` returns a
  // `std::chrono::nanoseconds`. Upstream `tf2_ros::Buffer` is where those land,
  // because `rclcpp::Time` accepts the message stamp and `rclcpp::Duration`
  // accepts the chrono duration; without this overload the call resolves
  // against the `tf2::TimePoint` one and fails with "no known conversion for
  // argument 3 from `builtin_interfaces::msg::Time` to `const tf2::TimePoint&`"
  // - which is what `opennav_docking`'s trajectory controller hit.
  geometry_msgs::msg::TransformStamped lookupTransform(
    const std::string & target, const std::string & source,
    const rclcpp::Time & time,
    const rclcpp::Duration & timeout = rclcpp::Duration::from_nanoseconds(0))
  {
    return tf2::BufferCore::lookupTransform(
      target, source, detail::toTimePoint(time),
      tf2::Duration(timeout.nanoseconds()));
  }

  geometry_msgs::msg::TransformStamped lookupTransform(
    const std::string & target, const rclcpp::Time & target_time,
    const std::string & source, const rclcpp::Time & source_time,
    const std::string & fixed_frame, const tf2::Duration & timeout = tf2::Duration::zero())
  {
    return tf2::BufferCore::lookupTransform(
      target, detail::toTimePoint(target_time), source, detail::toTimePoint(source_time),
      fixed_frame, timeout);
  }

  geometry_msgs::msg::TransformStamped lookupTransform(
    const std::string & target, const tf2::TimePoint & target_time,
    const std::string & source, const tf2::TimePoint & source_time,
    const std::string & fixed_frame, const tf2::Duration & timeout = tf2::Duration::zero())
  {
    return tf2::BufferCore::lookupTransform(
      target, target_time, source, source_time, fixed_frame, timeout);
  }

  template<class T>
  void transform(const T & in, T & out, const std::string & target_frame)
  {
    transform(in, out, target_frame, tf2::Duration::zero());
  }

  template<class T>
  void transform(
    const T & in, T & out, const std::string & target_frame, const tf2::Duration &)
  {
    // The stamp the caller put on `in` is the instant it wants the transform
    // *at*; reading the newest one instead pairs every message with whatever
    // the tree moved on to while the message sat in a queue, which is exactly
    // how a scan ends up matched against the pose that comes after it.
    tf2::TimePoint stamp = tf2::TimePointZero;
    if constexpr (detail::has_stamp<T>::value) {
      stamp = tf2::timePointFromRosTime(in.header.stamp);
    }
    const auto tf_msg =
      tf2::BufferCore::lookupTransform(target_frame, in.header.frame_id, stamp);
    out = in;
    out.header.frame_id = target_frame;
    if constexpr (detail::has_pose_v<T>) {
      tf2::Transform transform_tf;
      tf2::fromMsg(tf_msg.transform, transform_tf);
      tf2::Transform input_tf;
      tf2::fromMsg(in.pose, input_tf);
      out.pose = tf2::toMsgPose(transform_tf * input_tf);
    } else if constexpr (detail::has_point_v<T>) {
      tf2::Transform transform_tf;
      tf2::fromMsg(tf_msg.transform, transform_tf);
      tf2::Vector3 v;
      tf2::fromMsg(in.point, v);
      const tf2::Vector3 r = transform_tf(v);
      out.point.x = r.x();
      out.point.y = r.y();
      out.point.z = r.z();
    } else {
      tf2::doTransform(in, out, tf_msg);
    }
  }

  template<class T>
  void transform(
    const T & in, T & out, const std::string & target_frame, float)
  {
    transform(in, out, target_frame, tf2::Duration::zero());
  }

  template<class T>
  T transform(const T & in, const std::string & target_frame)
  {
    T out;
    transform(in, out, target_frame);
    return out;
  }

  template<class T>
  T transform(const T & in, const std::string & target_frame, const tf2::Duration & d)
  {
    T out;
    transform(in, out, target_frame, d);
    return out;
  }

  template<class T>
  T transform(const T & in, const std::string & target_frame, float d)
  {
    T out;
    transform(in, out, target_frame, d);
    return out;
  }

  rclcpp::Duration getCacheLength() {return rclcpp::Duration::from_seconds(10.0);}

private:
  std::shared_ptr<rclcpp::Clock> clock_;
  std::shared_ptr<CreateTimerInterface> timer_interface_;
};

}  // namespace tf2_ros

#endif  // TF2_ROS__BUFFER_HPP_
