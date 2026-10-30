#ifndef ROSGRAPH_MSGS_MSG_SERVICE_HPP_
#define ROSGRAPH_MSGS_MSG_SERVICE_HPP_

#include "rosgraph_msgs/msg/interface_type.hpp"
#include "rosgraph_msgs/msg/qo_s_profile.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace rosgraph_msgs
{
namespace msg
{

struct Service
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rosgraph_msgs/msg/Service";
  using SharedPtr = std::shared_ptr<Service>;
  using ConstSharedPtr = std::shared_ptr<const Service>;
  using UniquePtr = std::unique_ptr<Service>;
  using _name_type = std::string;
  std::string name;
  using _request_type_type = ::rosgraph_msgs::msg::InterfaceType;
  ::rosgraph_msgs::msg::InterfaceType request_type;
  using _request_qos_type = ::rosgraph_msgs::msg::QoSProfile;
  ::rosgraph_msgs::msg::QoSProfile request_qos;
  using _response_type_type = ::rosgraph_msgs::msg::InterfaceType;
  ::rosgraph_msgs::msg::InterfaceType response_type;
  using _response_qos_type = ::rosgraph_msgs::msg::QoSProfile;
  ::rosgraph_msgs::msg::QoSProfile response_qos;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(name);
    ar(request_type);
    ar(request_qos);
    ar(response_type);
    ar(response_qos);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(name);
    ar(request_type);
    ar(request_qos);
    ar(response_type);
    ar(response_qos);
  }

  bool operator==(const Service & other) const
  {
    return name == other.name &&
      request_type == other.request_type &&
      request_qos == other.request_qos &&
      response_type == other.response_type &&
      response_qos == other.response_qos;
  }
  bool operator!=(const Service & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rosgraph_msgs

#endif  // ROSGRAPH_MSGS_MSG_SERVICE_HPP_
