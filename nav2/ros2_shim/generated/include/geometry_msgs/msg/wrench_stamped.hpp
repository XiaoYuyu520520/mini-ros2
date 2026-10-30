#ifndef GEOMETRY_MSGS_MSG_WRENCHSTAMPED_HPP_
#define GEOMETRY_MSGS_MSG_WRENCHSTAMPED_HPP_

#include "geometry_msgs/msg/wrench.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct WrenchStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/WrenchStamped";
  using SharedPtr = std::shared_ptr<WrenchStamped>;
  using ConstSharedPtr = std::shared_ptr<const WrenchStamped>;
  using UniquePtr = std::unique_ptr<WrenchStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _wrench_type = ::geometry_msgs::msg::Wrench;
  ::geometry_msgs::msg::Wrench wrench;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(wrench);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(wrench);
  }

  bool operator==(const WrenchStamped & other) const
  {
    return header == other.header &&
      wrench == other.wrench;
  }
  bool operator!=(const WrenchStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_WRENCHSTAMPED_HPP_
