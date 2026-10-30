#ifndef TF2_ROS__TRANSFORM_BROADCASTER_HPP_
#define TF2_ROS__TRANSFORM_BROADCASTER_HPP_

#include <memory>
#include <vector>

#include "geometry_msgs/msg/transform_stamped.hpp"
#include "rclcpp/rclcpp.hpp"
#include "tf2_msgs/msg/tf_message.hpp"

namespace tf2_ros
{

class TransformBroadcaster
{
public:
  template<class NodeT>
  explicit TransformBroadcaster(const NodeT & node)
  {
    publisher_ = node->template create_publisher<tf2_msgs::msg::TFMessage>(
      "/tf", rclcpp::QoS(rclcpp::KeepLast(100)));
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
  static std::shared_ptr<TransformBroadcaster> create(const NodeT & node)
  {
    return std::make_shared<TransformBroadcaster>(node);
  }

private:
  std::shared_ptr<rclcpp::Publisher<tf2_msgs::msg::TFMessage>> publisher_;
};

}  // namespace tf2_ros

#endif  // TF2_ROS__TRANSFORM_BROADCASTER_HPP_
