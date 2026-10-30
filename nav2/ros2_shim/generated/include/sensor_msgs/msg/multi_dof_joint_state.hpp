#ifndef SENSOR_MSGS_MSG_MULTIDOFJOINTSTATE_HPP_
#define SENSOR_MSGS_MSG_MULTIDOFJOINTSTATE_HPP_

#include "geometry_msgs/msg/transform.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "geometry_msgs/msg/wrench.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct MultiDOFJointState
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/MultiDOFJointState";
  using SharedPtr = std::shared_ptr<MultiDOFJointState>;
  using ConstSharedPtr = std::shared_ptr<const MultiDOFJointState>;
  using UniquePtr = std::unique_ptr<MultiDOFJointState>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _joint_names_type = std::vector<std::string>;
  std::vector<std::string> joint_names;
  using _transforms_type = std::vector<::geometry_msgs::msg::Transform>;
  std::vector<::geometry_msgs::msg::Transform> transforms;
  using _twist_type = std::vector<::geometry_msgs::msg::Twist>;
  std::vector<::geometry_msgs::msg::Twist> twist;
  using _wrench_type = std::vector<::geometry_msgs::msg::Wrench>;
  std::vector<::geometry_msgs::msg::Wrench> wrench;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(joint_names);
    ar(transforms);
    ar(twist);
    ar(wrench);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(joint_names);
    ar(transforms);
    ar(twist);
    ar(wrench);
  }

  bool operator==(const MultiDOFJointState & other) const
  {
    return header == other.header &&
      joint_names == other.joint_names &&
      transforms == other.transforms &&
      twist == other.twist &&
      wrench == other.wrench;
  }
  bool operator!=(const MultiDOFJointState & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_MULTIDOFJOINTSTATE_HPP_
