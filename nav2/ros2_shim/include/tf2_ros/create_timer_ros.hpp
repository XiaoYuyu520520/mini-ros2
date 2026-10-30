#ifndef TF2_ROS__CREATE_TIMER_ROS_HPP_
#define TF2_ROS__CREATE_TIMER_ROS_HPP_

#include <memory>

#include "rclcpp/rclcpp.hpp"

namespace tf2_ros
{

class CreateTimerInterface
{
public:
  virtual ~CreateTimerInterface() = default;
};

class CreateTimerROS : public CreateTimerInterface
{
public:
  CreateTimerROS(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr,
    rclcpp::node_interfaces::NodeTimersInterface::SharedPtr,
    rclcpp::CallbackGroup::SharedPtr = nullptr)
  {
  }

  template<class NodeT>
  CreateTimerROS(const NodeT &, rclcpp::CallbackGroup::SharedPtr = nullptr)
  {
  }
};

}  // namespace tf2_ros

#endif  // TF2_ROS__CREATE_TIMER_ROS_HPP_
