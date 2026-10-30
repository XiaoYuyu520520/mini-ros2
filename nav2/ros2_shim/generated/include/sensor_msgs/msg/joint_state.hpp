#ifndef SENSOR_MSGS_MSG_JOINTSTATE_HPP_
#define SENSOR_MSGS_MSG_JOINTSTATE_HPP_

#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct JointState
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/JointState";
  using SharedPtr = std::shared_ptr<JointState>;
  using ConstSharedPtr = std::shared_ptr<const JointState>;
  using UniquePtr = std::unique_ptr<JointState>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _name_type = std::vector<std::string>;
  std::vector<std::string> name;
  using _position_type = std::vector<double>;
  std::vector<double> position;
  using _velocity_type = std::vector<double>;
  std::vector<double> velocity;
  using _effort_type = std::vector<double>;
  std::vector<double> effort;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(name);
    ar(position);
    ar(velocity);
    ar(effort);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(name);
    ar(position);
    ar(velocity);
    ar(effort);
  }

  bool operator==(const JointState & other) const
  {
    return header == other.header &&
      name == other.name &&
      position == other.position &&
      velocity == other.velocity &&
      effort == other.effort;
  }
  bool operator!=(const JointState & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_JOINTSTATE_HPP_
