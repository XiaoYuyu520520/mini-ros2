#ifndef ROSGRAPH_MSGS_MSG_TOPIC_HPP_
#define ROSGRAPH_MSGS_MSG_TOPIC_HPP_

#include "rosgraph_msgs/msg/interface_type.hpp"
#include "rosgraph_msgs/msg/qo_s_profile.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace rosgraph_msgs
{
namespace msg
{

struct Topic
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rosgraph_msgs/msg/Topic";
  using SharedPtr = std::shared_ptr<Topic>;
  using ConstSharedPtr = std::shared_ptr<const Topic>;
  using UniquePtr = std::unique_ptr<Topic>;
  using _name_type = std::string;
  std::string name;
  using _type_type = ::rosgraph_msgs::msg::InterfaceType;
  ::rosgraph_msgs::msg::InterfaceType type;
  using _qos_type = ::rosgraph_msgs::msg::QoSProfile;
  ::rosgraph_msgs::msg::QoSProfile qos;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(name);
    ar(type);
    ar(qos);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(name);
    ar(type);
    ar(qos);
  }

  bool operator==(const Topic & other) const
  {
    return name == other.name &&
      type == other.type &&
      qos == other.qos;
  }
  bool operator!=(const Topic & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rosgraph_msgs

#endif  // ROSGRAPH_MSGS_MSG_TOPIC_HPP_
