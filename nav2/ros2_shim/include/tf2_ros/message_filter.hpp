#ifndef TF2_ROS__MESSAGE_FILTER_HPP_
#define TF2_ROS__MESSAGE_FILTER_HPP_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "message_filters/subscriber.hpp"
#include "rclcpp/rclcpp.hpp"
#include "tf2/buffer_core.hpp"
#include "tf2/time.hpp"
#include "tf2_ros/buffer.hpp"

namespace tf2_ros
{

/// What the ported code reaches for when it holds a filter it did not build
/// itself: `nav2_costmap_2d::ObstacleLayer` keeps its per-sensor filters as
/// `MessageFilterBase` so one vector can hold a LaserScan filter and a
/// PointCloud2 filter at once.
class MessageFilterBase
{
public:
  virtual ~MessageFilterBase() = default;

  /// The single frame messages have to be transformable into.
  virtual void setTargetFrame(const std::string & target_frame) = 0;
  /// The plural form: a message stamped in any of these frames is accepted.
  virtual void setTargetFrames(const std::vector<std::string> & target_frames) = 0;
  /// How long a message may wait for its transform before it is dropped.
  virtual void setTolerance(const tf2::Duration & tolerance) = 0;
  /// Drop whatever is buffered, so a message from before the filter was
  /// stopped is not delivered after it starts again.
  virtual void clear() = 0;

  /// Tolerance arrives as an `rclcpp::Duration` at every call site in the
  /// ported packages, and that type does not convert to `tf2::Duration` on its
  /// own, so the conversion lives here next to the virtual it feeds.
  void setTolerance(const rclcpp::Duration & tolerance)
  {
    setTolerance(std::chrono::nanoseconds(tolerance.nanoseconds()));
  }
};

/// Holds every message back until the transform it is going to be read with
/// exists.
///
/// Upstream's filter does this, and the port needs it for the same reason: the
/// topic carrying a sensor and the topic carrying the transforms are
/// independent streams, so a scan stamped T can be in hand while the transform
/// for T is still in flight -- measured here at one odometry sample, 20 ms,
/// whenever the two arrive in the other order. Delivering the message straight
/// away (what this filter used to do) hands the costmap a lookup at T against
/// a tree whose newest sample is T-20 ms, which is an extrapolation into the
/// future: the obstacle layer reports "TF Exception that should never happen"
/// and the scan never reaches the costmap at all.
///
/// A held message is retried on every spin of the node that owns the
/// subscription and is dropped once `tolerance` has passed, which is
/// upstream's `keep_alive`: either the transform turns up on a later spin or
/// the message was never going to be usable.
template<class MessageT>
class MessageFilter : public MessageFilterBase
{
public:
  using SharedPtr = std::shared_ptr<MessageFilter<MessageT>>;
  using Callback = std::function<void(const std::shared_ptr<const MessageT> &)>;

  template<class SubscriberT>
  MessageFilter(
    SubscriberT & subscriber, tf2_ros::Buffer & buffer, const std::string & target_frame,
    uint32_t queue_size, rclcpp::node_interfaces::NodeLoggingInterface::SharedPtr logging,
    rclcpp::node_interfaces::NodeClockInterface::SharedPtr clock,
    tf2::Duration tolerance = tf2::Duration::zero())
  {
    // The node is reached through the clock interface: nav2's
    // `create_message_filter` hands over the interfaces rather than the node
    // itself, and the node is what has to be spun for a held message to be
    // retried (see rclcpp/node_interfaces.hpp).
    start(&buffer, target_frame, queue_size, tolerance, node_of(clock));
    (void)logging;
    connect(subscriber);
  }

  template<class SubscriberT, class NodeT>
  MessageFilter(
    SubscriberT & subscriber, tf2_ros::Buffer & buffer, const std::string & target_frame,
    uint32_t queue_size, const NodeT & node, tf2::Duration tolerance = tf2::Duration::zero())
  {
    start(&buffer, target_frame, queue_size, tolerance, as_node(node));
    connect(subscriber);
  }

  /// The retry registered on the node outlives the filter: a node is spun by
  /// whoever owns it, not by the layer that built the filter, so the filter
  /// has to leave nothing behind that could call back into a layer that is
  /// being torn down.
  ~MessageFilter() override
  {
    shared_->alive.store(false);
    std::lock_guard<std::recursive_mutex> lock(shared_->mutex);
    shared_->callback = nullptr;
    shared_->queue.clear();
  }

  message_filters::Connection registerCallback(Callback callback)
  {
    std::lock_guard<std::recursive_mutex> lock(shared_->mutex);
    shared_->callback = std::move(callback);
    message_filters::Connection connection;
    connection.setDisconnectCallback([this]() {
        std::lock_guard<std::recursive_mutex> guard(shared_->mutex);
        shared_->callback = nullptr;
      });
    return connection;
  }

  void setTargetFrame(const std::string & frame) override
  {
    target_frame_ = frame;
    target_frames_.clear();
    setSharedTargets({frame.empty() ? std::vector<std::string>() :
      std::vector<std::string>{frame}});
  }

  void setTargetFrames(const std::vector<std::string> & frames) override
  {
    target_frames_ = frames;
    if (!frames.empty()) {
      target_frame_ = frames.front();
    }
    setSharedTargets(frames);
  }

  void setTolerance(const tf2::Duration & tolerance) override
  {
    tolerance_ = tolerance;
    std::lock_guard<std::recursive_mutex> lock(shared_->mutex);
    shared_->tolerance = tolerance;
  }

  using MessageFilterBase::setTolerance;

  void clear() override
  {
    std::lock_guard<std::recursive_mutex> lock(shared_->mutex);
    shared_->queue.clear();
  }

  const std::string & getTargetFrame() const {return target_frame_;}
  const std::vector<std::string> & getTargetFrames() const {return target_frames_;}
  const tf2::Duration & getTolerance() const {return tolerance_;}

private:
  /// One message waiting for the transform it will be read with.
  struct Pending
  {
    std::shared_ptr<const MessageT> message;
    std::chrono::steady_clock::time_point deadline;
  };

  /// What a delivered (or dropped) message needs, kept apart from the filter
  /// so that the retry registered on the node can hold it: the retry must
  /// still find an empty queue after the filter is gone, not a dangling
  /// `this`.
  struct Shared
  {
    std::recursive_mutex mutex;
    std::deque<Pending> queue;
    Callback callback;
    std::vector<std::string> target_frames;
    tf2_ros::Buffer * buffer = nullptr;
    tf2::Duration tolerance = tf2::Duration::zero();
    uint32_t queue_size = 50;
    uint64_t dropped = 0;
    bool retry_registered = false;
    std::atomic<bool> alive{true};
  };

  static rclcpp::Node * node_of(
    const rclcpp::node_interfaces::NodeClockInterface::SharedPtr & clock)
  {
    return clock ? clock->node : nullptr;
  }

  /// The `const NodeT &` constructor is handed the node itself at every call
  /// site that has one, in whatever shape that call site holds it: the object,
  /// a bare pointer or a smart one.
  template<class T>
  static rclcpp::Node * as_node(const T & value)
  {
    if constexpr (std::is_base_of_v<rclcpp::Node, std::decay_t<T>>) {
      return const_cast<rclcpp::Node *>(static_cast<const rclcpp::Node *>(&value));
    } else {
      return value == nullptr ? nullptr : as_node(*value);
    }
  }

  void start(
    tf2_ros::Buffer * buffer, const std::string & target_frame, uint32_t queue_size,
    tf2::Duration tolerance, rclcpp::Node * node)
  {
    target_frame_ = target_frame;
    tolerance_ = tolerance;
    target_frames_ = target_frame.empty() ?
      std::vector<std::string>() : std::vector<std::string>{target_frame};
    {
      std::lock_guard<std::recursive_mutex> lock(shared_->mutex);
      shared_->buffer = buffer;
      shared_->target_frames = target_frames_;
      shared_->tolerance = tolerance;
      shared_->queue_size = queue_size == 0 ? 1 : queue_size;
    }
    if (node != nullptr) {
      std::shared_ptr<Shared> shared = shared_;
      node->add_pollable([shared]() {retry(shared);});
      shared_->retry_registered = true;
    }
  }

  template<class SubscriberT>
  void connect(SubscriberT & subscriber)
  {
    std::shared_ptr<Shared> shared = shared_;
    subscriber.registerCallback(
      [shared](const std::shared_ptr<const MessageT> & message) {
        if (!message || !shared->alive.load()) {
          return;
        }
        if (transformable(*shared, message)) {
          deliver(shared, message);
          return;
        }
        if (!shared->retry_registered) {
          // Nothing will come back for a held message, so holding it would
          // lose it for good; the reader gets the old behaviour instead.
          deliver(shared, message);
          return;
        }
        std::lock_guard<std::recursive_mutex> lock(shared->mutex);
        if (!shared->alive.load()) {
          return;
        }
        while (shared->queue.size() >= shared->queue_size) {
          shared->queue.pop_front();
          ++shared->dropped;
        }
        shared->queue.push_back(
          Pending{message, std::chrono::steady_clock::now() + shared->tolerance});
      });
  }

  void setSharedTargets(const std::vector<std::string> & frames)
  {
    std::lock_guard<std::recursive_mutex> lock(shared_->mutex);
    shared_->target_frames = frames;
  }

  /// Is the transform this message will be read with in the buffer already? A
  /// message with no frame, or one stamped at zero (which asks for whatever
  /// the newest transform is) cannot be waited for and is always let through.
  static bool transformable(
    Shared & shared, const std::shared_ptr<const MessageT> & message)
  {
    std::lock_guard<std::recursive_mutex> lock(shared.mutex);
    if (shared.buffer == nullptr || shared.target_frames.empty()) {
      return true;
    }
    const std::string & source = message->header.frame_id;
    if (source.empty()) {
      return true;
    }
    const tf2::TimePoint time = tf2::timePointFromRosTime(message->header.stamp);
    if (time == tf2::TimePointZero) {
      return true;
    }
    for (const auto & target : shared.target_frames) {
      // The error-message overload is the only unambiguous one: both
      // candidates take the same three arguments and default the fourth.
      if (target.empty() || target == source ||
        shared.buffer->canTransform(target, source, time, static_cast<std::string *>(nullptr)))
      {
        return true;
      }
    }
    return false;
  }

  static void deliver(
    const std::shared_ptr<Shared> & shared,
    const std::shared_ptr<const MessageT> & message)
  {
    Callback callback;
    {
      std::lock_guard<std::recursive_mutex> lock(shared->mutex);
      callback = shared->callback;
    }
    if (callback) {
      callback(message);
    }
  }

  /// One turn of the retry: the front of the queue is delivered as soon as its
  /// transform is there and dropped once it has waited out `tolerance`.
  /// Messages leave in the order they arrived, so one whose transform is
  /// missing for longer than usual does not let a later one overtake it.
  static void retry(const std::shared_ptr<Shared> & shared)
  {
    for (;;) {
      std::shared_ptr<const MessageT> message;
      Callback callback;
      bool expired = false;
      {
        std::lock_guard<std::recursive_mutex> lock(shared->mutex);
        if (!shared->alive.load() || shared->queue.empty()) {
          return;
        }
        Pending & front = shared->queue.front();
        if (transformable(*shared, front.message)) {
          message = front.message;
          callback = shared->callback;
          shared->queue.pop_front();
        } else if (std::chrono::steady_clock::now() >= front.deadline) {
          message = front.message;
          expired = true;
          shared->queue.pop_front();
          ++shared->dropped;
        } else {
          return;
        }
      }
      if (expired) {
        reportDropped(shared, *message);
        continue;
      }
      if (callback) {
        callback(message);
      }
    }
  }

  static void reportDropped(const std::shared_ptr<Shared> & shared, const MessageT & message)
  {
    std::string targets;
    uint64_t dropped = 0;
    double tolerance_ms = 0.0;
    {
      std::lock_guard<std::recursive_mutex> lock(shared->mutex);
      dropped = shared->dropped;
      tolerance_ms = tf2::durationToSec(shared->tolerance) * 1e3;
      for (const auto & target : shared->target_frames) {
        if (!targets.empty()) {
          targets += ", ";
        }
        targets += target;
      }
    }
    // Only the first drop is interesting - after that either the transform
    // turns up or every message is going the same way, and the count carries
    // that.
    if (dropped != 1 && dropped % 100 != 0) {
      return;
    }
    RCLCPP_WARN(
      rclcpp::get_logger("tf2_ros"),
      "Dropped %llu message(s) stamped %.3f in frame '%s': no transform to '%s' within %.0f ms.",
      static_cast<unsigned long long>(dropped),
      static_cast<double>(message.header.stamp.sec) + message.header.stamp.nanosec * 1e-9,
      message.header.frame_id.c_str(), targets.c_str(), tolerance_ms);
  }

  std::string target_frame_;
  std::vector<std::string> target_frames_;
  tf2::Duration tolerance_ = tf2::Duration::zero();
  std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
};

}  // namespace tf2_ros

#endif  // TF2_ROS__MESSAGE_FILTER_HPP_
