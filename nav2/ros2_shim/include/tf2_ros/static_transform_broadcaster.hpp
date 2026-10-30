#ifndef TF2_ROS__STATIC_TRANSFORM_BROADCASTER_HPP_
#define TF2_ROS__STATIC_TRANSFORM_BROADCASTER_HPP_

#include <memory>
#include <vector>

#include "geometry_msgs/msg/transform_stamped.hpp"
#include "rclcpp/rclcpp.hpp"
#include "tf2_msgs/msg/tf_message.hpp"

namespace tf2_ros
{

class StaticTransformBroadcaster
{
public:
  template<class NodeT>
  explicit StaticTransformBroadcaster(const NodeT & node)
  {
    publisher_ = node->template create_publisher<tf2_msgs::msg::TFMessage>(
      "/tf_static", rclcpp::QoS(rclcpp::KeepLast(100)).transient_local());
  }

  void sendTransform(const tf2_msgs::msg::TFMessage & message)
  {
    publisher_->publish(message);
  }

  void sendTransform(const geometry_msgs::msg::TransformStamped & transform)
  {
    tf2_msgs::msg::TFMessage message;
    message.transforms.push_back(transform);
    publisher_->publish(message);
  }

  void sendTransform(const std::vector<geometry_msgs::msg::TransformStamped> & transforms)
  {
    tf2_msgs::msg::TFMessage message;
    message.transforms = transforms;
    publisher_->publish(message);
  }

  template<class NodeT>
  static std::shared_ptr<StaticTransformBroadcaster> create(const NodeT & node)
  {
    return std::make_shared<StaticTransformBroadcaster>(node);
  }

private:
  std::shared_ptr<rclcpp::Publisher<tf2_msgs::msg::TFMessage>> publisher_;
};

}  // namespace tf2_ros

#endif  // TF2_ROS__STATIC_TRANSFORM_BROADCASTER_HPP_
