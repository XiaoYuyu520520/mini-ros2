#ifndef TF2_ROS__TRANSFORM_LISTENER_HPP_
#define TF2_ROS__TRANSFORM_LISTENER_HPP_

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "rclcpp/rclcpp.hpp"
#include "tf2/buffer_core.hpp"
#include "tf2_msgs/msg/tf_message.hpp"

namespace tf2_ros
{

class TransformListener
{
public:
  // Upstream, this overload conjures a private node for the subscriptions and
  // (optionally) spins it on a dedicated thread. Call sites such as
  // slam_toolbox's `TransformListener(*tf_buffer)` hand it nothing else to
  // subscribe with, so the shim does the same thing -- minus the upstream
  // executor, which the shim node's own poll loop replaces.
  explicit TransformListener(tf2::BufferCore & buffer, bool spin_thread = true)
  : buffer_(&buffer)
  {
    node_ = std::make_shared<rclcpp::Node>(
      "transform_listener_impl_" + std::to_string(
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this))));
    subscribe(node_);
    if (spin_thread) {
      startThread();
    }
  }

  template<class NodeT>
  TransformListener(tf2::BufferCore & buffer, const NodeT & node, bool = true)
  : buffer_(&buffer)
  {
    subscribe(node);
  }

  virtual ~TransformListener()
  {
    stop_thread_.store(true);
    if (spin_thread_.joinable()) {
      spin_thread_.join();
    }
  }

  virtual void subscription_callback(
    tf2_msgs::msg::TFMessage::ConstSharedPtr message, bool is_static)
  {
    for (const auto & transform : message->transforms) {
      buffer_->setTransform(transform, is_static);
    }
  }

  template<class NodeT>
  void subscribe(const NodeT & node)
  {
    tf_subscription_ = rclcpp::create_subscription<tf2_msgs::msg::TFMessage>(
      node->get_node_parameters_interface(), node->get_node_topics_interface(),
      "/tf", rclcpp::QoS(rclcpp::KeepLast(100)),
      [this](const tf2_msgs::msg::TFMessage::ConstSharedPtr message) {
        subscription_callback(message, false);
      },
      rclcpp::SubscriptionOptions());
    static_subscription_ = rclcpp::create_subscription<tf2_msgs::msg::TFMessage>(
      node->get_node_parameters_interface(), node->get_node_topics_interface(),
      "/tf_static", rclcpp::QoS(rclcpp::KeepLast(100)).transient_local(),
      [this](const tf2_msgs::msg::TFMessage::ConstSharedPtr message) {
        subscription_callback(message, true);
      },
      rclcpp::SubscriptionOptions());
  }

protected:
  void startThread()
  {
    spin_thread_ = std::thread(
      [this]() {
        while (!stop_thread_.load()) {
          if (node_) {
            node_->spin_once();
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      });
  }

  tf2::BufferCore * buffer_;
  rclcpp::Node::SharedPtr node_;
  std::thread spin_thread_;
  std::atomic<bool> stop_thread_{false};
  typename rclcpp::Subscription<tf2_msgs::msg::TFMessage>::SharedPtr tf_subscription_;
  typename rclcpp::Subscription<tf2_msgs::msg::TFMessage>::SharedPtr static_subscription_;
};

}  // namespace tf2_ros

#endif  // TF2_ROS__TRANSFORM_LISTENER_HPP_
