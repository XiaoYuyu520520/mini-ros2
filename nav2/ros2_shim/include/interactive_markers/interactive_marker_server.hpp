#ifndef INTERACTIVE_MARKERS__INTERACTIVE_MARKER_SERVER_HPP_
#define INTERACTIVE_MARKERS__INTERACTIVE_MARKER_SERVER_HPP_

// A working subset of upstream `interactive_markers::InteractiveMarkerServer`.
//
// Upstream implements the protocol on two topics -- `<ns>/update` carrying
// `InteractiveMarkerUpdate` and `<ns>/feedback` carrying `InteractiveMarkerFeedback`
// -- and that is exactly what is implemented here, so an RViz2 client can still
// grab and drag the loop-closure nodes published by slam_toolbox. What is *not*
// here is the background update thread and the menu plumbing; markers are
// flushed synchronously from `applyChanges()`.

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "rclcpp/node.hpp"
#include "rclcpp/node_interfaces.hpp"
#include "rclcpp/qos.hpp"
#include "visualization_msgs/msg/interactive_marker.hpp"
#include "visualization_msgs/msg/interactive_marker_feedback.hpp"
#include "visualization_msgs/msg/interactive_marker_pose.hpp"
#include "visualization_msgs/msg/interactive_marker_update.hpp"

namespace interactive_markers
{

using FeedbackCallback = std::function<void(
      const visualization_msgs::msg::InteractiveMarkerFeedback::ConstSharedPtr &)>;

class InteractiveMarkerServer
{
public:
  InteractiveMarkerServer(
    const std::string & marker_namespace,
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr base_interface,
    rclcpp::node_interfaces::NodeClockInterface::SharedPtr clock_interface,
    rclcpp::node_interfaces::NodeLoggingInterface::SharedPtr logging_interface,
    rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics_interface,
    rclcpp::node_interfaces::NodeServicesInterface::SharedPtr)
  : namespace_(marker_namespace)
  {
    (void)logging_interface;
    if (clock_interface) {
      clock_ = clock_interface->get_clock();
    }
    rclcpp::Node * node = base_interface ? base_interface->node : nullptr;
    if (node == nullptr && topics_interface) {
      node = topics_interface->node;
    }
    bind(node, rclcpp::QoS(100));
  }

  // `InteractiveMarkerServer(ns, shared_from_this())` -- the spelling upstream
  // slam_toolbox uses in merge_maps_kinematic.
  InteractiveMarkerServer(
    const std::string & marker_namespace, rclcpp::Node::SharedPtr node,
    const rclcpp::QoS & qos = rclcpp::QoS(100))
  : namespace_(marker_namespace)
  {
    bind(node.get(), qos);
  }

  ~InteractiveMarkerServer() = default;

  InteractiveMarkerServer(const InteractiveMarkerServer &) = delete;
  InteractiveMarkerServer & operator=(const InteractiveMarkerServer &) = delete;

  void insert(
    const visualization_msgs::msg::InteractiveMarker & marker,
    FeedbackCallback feedback_cb = nullptr)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto & entry = markers_[marker.name];
    entry.marker = marker;
    entry.callback = std::move(feedback_cb);
    entry.is_new = true;
    if (entry.is_new) {
      pending_inserts_.push_back(marker.name);
    }
  }

  void erase(const std::string & name)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (markers_.erase(name) > 0) {
      pending_inserts_.erase(
        std::remove(pending_inserts_.begin(), pending_inserts_.end(), name),
        pending_inserts_.end());
      pending_erases_.push_back(name);
    }
  }

  void clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto & kv : markers_) {
      pending_erases_.push_back(kv.first);
    }
    markers_.clear();
    pending_inserts_.clear();
    applyChangesLocked();
  }

  void setCallback(const std::string & name, FeedbackCallback feedback_cb)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = markers_.find(name);
    if (it != markers_.end()) {
      it->second.callback = std::move(feedback_cb);
    }
  }

  void applyChanges()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    applyChangesLocked();
  }

  // Upstream takes a ThreadedExecutor here; the shim already polls every
  // subscription from the node's own spin loop, so there is nothing to do.
  void shutdown() {}

  bool get(const std::string & name, visualization_msgs::msg::InteractiveMarker & marker) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = markers_.find(name);
    if (it == markers_.end()) {
      return false;
    }
    marker = it->second.marker;
    return true;
  }

private:
  void bind(rclcpp::Node * node, const rclcpp::QoS & qos)
  {
    if (node == nullptr) {
      throw std::runtime_error("InteractiveMarkerServer requires a live rclcpp::Node");
    }
    node_ = node;
    if (clock_ == nullptr) {
      clock_ = node_->get_clock();
    }

    update_publisher_ = node_->create_publisher<visualization_msgs::msg::InteractiveMarkerUpdate>(
      namespace_ + "/update", qos);
    feedback_subscription_ =
      node_->create_subscription<visualization_msgs::msg::InteractiveMarkerFeedback>(
      namespace_ + "/feedback", qos,
      [this](visualization_msgs::msg::InteractiveMarkerFeedback::ConstSharedPtr feedback) {
        dispatchFeedback(feedback);
      });
  }

  struct Entry
  {
    visualization_msgs::msg::InteractiveMarker marker;
    FeedbackCallback callback;
    bool is_new = false;
  };

  void applyChangesLocked()
  {
    if (update_publisher_ == nullptr) {
      return;
    }
    if (pending_inserts_.empty() && pending_erases_.empty()) {
      return;
    }

    visualization_msgs::msg::InteractiveMarkerUpdate update;
    update.server_id = namespace_;
    update.seq_num = ++seq_num_;
    update.type = visualization_msgs::msg::InteractiveMarkerUpdate::UPDATE;
    for (const auto & name : pending_inserts_) {
      auto it = markers_.find(name);
      if (it != markers_.end()) {
        update.markers.push_back(it->second.marker);
      }
    }
    update.erases = pending_erases_;
    pending_inserts_.clear();
    pending_erases_.clear();
    update_publisher_->publish(update);
  }

  void dispatchFeedback(
    const visualization_msgs::msg::InteractiveMarkerFeedback::ConstSharedPtr & feedback)
  {
    FeedbackCallback callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = markers_.find(feedback->marker_name);
      if (it == markers_.end()) {
        return;
      }
      callback = it->second.callback;
    }
    if (callback) {
      callback(feedback);
    }
  }

  std::string namespace_;
  rclcpp::Node * node_ = nullptr;
  rclcpp::Clock::SharedPtr clock_;
  typename rclcpp::Publisher<visualization_msgs::msg::InteractiveMarkerUpdate>::SharedPtr
    update_publisher_;
  typename rclcpp::Subscription<visualization_msgs::msg::InteractiveMarkerFeedback>::SharedPtr
    feedback_subscription_;

  mutable std::mutex mutex_;
  std::map<std::string, Entry> markers_;
  std::vector<std::string> pending_inserts_;
  std::vector<std::string> pending_erases_;
  std::uint64_t seq_num_ = 0;
};

}  // namespace interactive_markers

#endif  // INTERACTIVE_MARKERS__INTERACTIVE_MARKER_SERVER_HPP_
