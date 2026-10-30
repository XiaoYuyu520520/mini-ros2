#ifndef TF2__BUFFER_CORE_HPP_
#define TF2__BUFFER_CORE_HPP_

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "geometry_msgs/msg/transform_stamped.hpp"
#include "tf2/exceptions.hpp"
#include "tf2/time.hpp"
#include "tf2/LinearMath/Transform.hpp"

namespace tf2
{

/// One broadcast of a frame, at the instant it was stamped with.
struct TransformSample
{
  TimePoint stamp;
  tf2::Transform transform;
};

/// Blend two transforms: the origin travels the straight line between them and
/// the rotation takes the shorter arc, which is what tf2 does between the two
/// samples that bracket a lookup.
inline tf2::Transform interpolate(
  const tf2::Transform & before, const tf2::Transform & after, double alpha)
{
  const tf2::Vector3 origin =
    before.getOrigin() + (after.getOrigin() - before.getOrigin()) * alpha;
  tf2::Quaternion qa, qb;
  before.getBasis().getRotation(qa);
  after.getBasis().getRotation(qb);
  if (qa.dot(qb) < 0.0) {
    qb = tf2::Quaternion(-qb.x(), -qb.y(), -qb.z(), -qb.w());
  }
  qa.normalize();
  qb.normalize();
  return tf2::Transform(qa.slerp(qb, alpha), origin);
}

/// The transform tree, with a time-ordered history per frame.
///
/// Upstream keeps `cache_time` worth of samples behind the newest one and
/// answers a lookup by interpolating between the samples around the requested
/// instant. The shim has to do the same: a consumer that pairs a scan with the
/// odometry pose stamped on it -- slam_toolbox's `getOdomPose`, AMCL, the
/// costmap layers -- reads whatever the buffer holds at that instant, and a
/// buffer that only ever knows the newest transform hands it the *next* pose
/// instead. That mismatch is what made the mapping run drift.
class BufferCore
{
public:
  explicit BufferCore(Duration cache_time = std::chrono::seconds(10))
  : cache_time_(cache_time)
  {
  }

  void clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    frames_.clear();
  }

  bool frameExists(const std::string & frame) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return frames_.find(frame) != frames_.end();
  }

  std::string allFramesAsString() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string out;
    for (const auto & entry : frames_) {
      out += entry.first;
      out += " (->";
      out += entry.second.parent;
      out += ") ";
    }
    return out;
  }

  void setTransform(const geometry_msgs::msg::TransformStamped & msg, bool is_static = false)
  {
    const tf2::Quaternion q(
      msg.transform.rotation.x, msg.transform.rotation.y,
      msg.transform.rotation.z, msg.transform.rotation.w);
    const tf2::Vector3 v(
      msg.transform.translation.x, msg.transform.translation.y,
      msg.transform.translation.z);
    // A static transform describes one fixed relation, so it has no instant to
    // be filed under; `TimePointZero` makes every timed lookup find it.
    const TimePoint stamp =
      is_static ? TimePointZero : timePointFromRosTime(msg.header.stamp);

    std::lock_guard<std::mutex> lock(mutex_);
    FrameData & frame = frames_[msg.child_frame_id];
    if (frame.is_static != is_static) {
      // A frame that used to be static and is now being driven has to forget
      // the fixed sample, and vice versa.
      frame.samples.clear();
      frame.is_static = is_static;
    }
    frame.parent = msg.header.frame_id;

    const TransformSample sample{stamp, tf2::Transform(q, v)};
    std::vector<TransformSample> & samples = frame.samples;
    if (samples.empty() || stamp >= samples.back().stamp) {
      if (!samples.empty() && stamp == samples.back().stamp) {
        samples.back() = sample;
      } else {
        samples.push_back(sample);
      }
    } else {
      const auto pos = std::lower_bound(
        samples.begin(), samples.end(), stamp,
        [](const TransformSample & entry, const TimePoint & time) {return entry.stamp < time;});
      if (pos != samples.end() && pos->stamp == stamp) {
        *pos = sample;
      } else {
        samples.insert(pos, sample);
      }
    }

    // The newest sample defines the window; two samples always survive so that
    // a lookup at the very edge of it still has something to interpolate
    // between.
    const TimePoint cutoff = samples.back().stamp - cache_time_;
    while (samples.size() > 2 && samples[1].stamp < cutoff) {
      samples.erase(samples.begin());
    }
    while (samples.size() > kMaxSamples) {
      samples.erase(samples.begin());
    }
  }

  bool canTransform(const std::string & target, const std::string & source) const
  {
    return canTransform(target, source, TimePointZero);
  }

  bool canTransform(
    const std::string & target, const std::string & source, const tf2::TimePoint & time) const
  {
    try {
      lookupTransform(target, source, time);
      return true;
    } catch (const tf2::TransformException &) {
      return false;
    }
  }

  bool canTransform(
    const std::string & target, const std::string & source, const tf2::TimePoint & time,
    const Duration &) const
  {
    return canTransform(target, source, time);
  }

  geometry_msgs::msg::TransformStamped lookupTransform(
    const std::string & target_frame, const std::string & source_frame) const
  {
    return lookupTransform(target_frame, source_frame, TimePointZero);
  }

  geometry_msgs::msg::TransformStamped lookupTransform(
    const std::string & target_frame, const std::string & source_frame,
    const tf2::TimePoint & time, const Duration & = Duration::zero()) const
  {
    std::lock_guard<std::mutex> lock(mutex_);

    geometry_msgs::msg::TransformStamped out;
    out.header.frame_id = target_frame;
    out.child_frame_id = source_frame;
    out.header.stamp = timePointToRosTime(time);

    if (target_frame == source_frame) {
      out.transform.rotation.w = 1.0;
      return out;
    }

    // Only the links *below* the deepest frame both sides share are needed.
    // Walking each side all the way to the root instead would drag in links
    // that have nothing to do with the answer -- `map -> odom`, published a
    // few milliseconds behind the scan that is being looked up, would make
    // every `odom -> base_footprint` lookup an extrapolation.
    const std::vector<std::string> target_chain = ancestry(target_frame);
    const std::vector<std::string> source_chain = ancestry(source_frame);

    std::string common;
    for (const auto & frame : source_chain) {
      if (std::find(target_chain.begin(), target_chain.end(), frame) != target_chain.end()) {
        common = frame;
        break;
      }
    }
    if (common.empty()) {
      throw tf2::ConnectivityException(
        "no common ancestor between '" + target_frame + "' and '" + source_frame + "'");
    }

    const tf2::Transform result =
      composeUpTo(target_frame, common, time).inverse() * composeUpTo(source_frame, common, time);

    const tf2::Vector3 origin = result.getOrigin();
    const tf2::Quaternion rotation = result.getRotation();
    out.transform.translation.x = origin.x();
    out.transform.translation.y = origin.y();
    out.transform.translation.z = origin.z();
    out.transform.rotation.x = rotation.x();
    out.transform.rotation.y = rotation.y();
    out.transform.rotation.z = rotation.z();
    out.transform.rotation.w = rotation.w();
    return out;
  }

  /// `lookupTransform(target, target_time, source, source_time, fixed_frame)`
  /// asks for the transform between two *different* instants, which needs the
  /// fixed frame to hang the two poses off. The shim's frames are anchored
  /// anyway, so the two instants are resolved independently.
  geometry_msgs::msg::TransformStamped lookupTransform(
    const std::string & target_frame, const tf2::TimePoint &,
    const std::string & source_frame, const tf2::TimePoint &,
    const std::string &, const Duration & = Duration::zero()) const
  {
    return lookupTransform(target_frame, source_frame, TimePointZero);
  }

private:
  struct FrameData
  {
    std::string parent;
    bool is_static = false;
    std::vector<TransformSample> samples;  // oldest first
  };

  static constexpr std::size_t kMaxSamples = 4096;

  tf2::Transform sampleAt(
    const FrameData & frame, const std::string & child, const TimePoint & time) const
  {
    if (frame.samples.empty()) {
      throw tf2::LookupException("frame '" + child + "' has no transform");
    }

    const TransformSample & newest = frame.samples.back();
    if (frame.is_static || time == TimePointZero || time == newest.stamp) {
      return newest.transform;
    }

    if (time > newest.stamp) {
      // Nothing has been broadcast for this instant yet. Upstream raises here
      // too, and consumers rely on it: slam_toolbox drops the scan instead of
      // matching it against the pose that comes after it.
      throw tf2::ExtrapolationException(
        "Lookup would require extrapolation into the future on frame '" + child + "'");
    }

    const TransformSample & oldest = frame.samples.front();
    if (time <= oldest.stamp) {
      return oldest.transform;
    }

    const auto upper = std::upper_bound(
      frame.samples.begin(), frame.samples.end(), time,
      [](const TimePoint & t, const TransformSample & entry) {return t < entry.stamp;});
    const TransformSample & after = *upper;
    const TransformSample & before = *(upper - 1);
    const Duration span = after.stamp - before.stamp;
    if (span.count() <= 0) {
      return after.transform;
    }
    const double alpha =
      static_cast<double>((time - before.stamp).count()) / static_cast<double>(span.count());
    return interpolate(before.transform, after.transform, alpha);
  }

  bool has_children(const std::string & frame) const
  {
    for (const auto & entry : frames_) {
      if (entry.second.parent == frame) {
        return true;
      }
    }
    return false;
  }

  /// `frame` followed by every ancestor above it, closest first.
  std::vector<std::string> ancestry(const std::string & frame) const
  {
    std::vector<std::string> chain;
    std::string current = frame;
    for (size_t depth = 0; depth < 256; ++depth) {
      if (chain.empty() && frames_.find(current) == frames_.end() && !has_children(current)) {
        throw tf2::LookupException("frame '" + frame + "' does not exist");
      }
      chain.push_back(current);
      auto it = frames_.find(current);
      if (it == frames_.end()) {
        // The root of the tree -- `map`, `odom`, ... -- is only ever a parent,
        // so nothing was ever published *as* that child frame.
        return chain;
      }
      if (it->second.parent.empty() || it->second.parent == current) {
        return chain;
      }
      current = it->second.parent;
    }
    throw tf2::TransformException("transform chain too deep (cycle?) at '" + frame + "'");
  }

  /// The pose of `frame` in `ancestor`, each link taken at `time`.
  tf2::Transform composeUpTo(
    const std::string & frame, const std::string & ancestor, const TimePoint & time) const
  {
    tf2::Transform result;
    result.setIdentity();
    std::string current = frame;
    for (size_t depth = 0; depth < 256; ++depth) {
      if (current == ancestor) {
        return result;
      }
      auto it = frames_.find(current);
      if (it == frames_.end()) {
        return result;
      }
      result = sampleAt(it->second, current, time) * result;
      if (it->second.parent.empty() || it->second.parent == current) {
        return result;
      }
      current = it->second.parent;
    }
    throw tf2::TransformException("transform chain too deep (cycle?) at '" + frame + "'");
  }

  mutable std::mutex mutex_;
  Duration cache_time_;
  std::map<std::string, FrameData> frames_;
};

}  // namespace tf2

#endif  // TF2__BUFFER_CORE_HPP_
