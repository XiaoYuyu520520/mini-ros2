#ifndef ROSGRAPH_MSGS_MSG_INTERFACETYPE_HPP_
#define ROSGRAPH_MSGS_MSG_INTERFACETYPE_HPP_

#include "rosgraph_msgs/msg/type_hash.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace rosgraph_msgs
{
namespace msg
{

struct InterfaceType
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rosgraph_msgs/msg/InterfaceType";
  using SharedPtr = std::shared_ptr<InterfaceType>;
  using ConstSharedPtr = std::shared_ptr<const InterfaceType>;
  using UniquePtr = std::unique_ptr<InterfaceType>;
  using _name_type = std::string;
  std::string name;
  using _hash_type = ::rosgraph_msgs::msg::TypeHash;
  ::rosgraph_msgs::msg::TypeHash hash;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(name);
    ar(hash);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(name);
    ar(hash);
  }

  bool operator==(const InterfaceType & other) const
  {
    return name == other.name &&
      hash == other.hash;
  }
  bool operator!=(const InterfaceType & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rosgraph_msgs

#endif  // ROSGRAPH_MSGS_MSG_INTERFACETYPE_HPP_
