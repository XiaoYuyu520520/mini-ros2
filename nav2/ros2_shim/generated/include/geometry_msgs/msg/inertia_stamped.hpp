#ifndef GEOMETRY_MSGS_MSG_INERTIASTAMPED_HPP_
#define GEOMETRY_MSGS_MSG_INERTIASTAMPED_HPP_

#include "geometry_msgs/msg/inertia.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct InertiaStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/InertiaStamped";
  using SharedPtr = std::shared_ptr<InertiaStamped>;
  using ConstSharedPtr = std::shared_ptr<const InertiaStamped>;
  using UniquePtr = std::unique_ptr<InertiaStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _inertia_type = ::geometry_msgs::msg::Inertia;
  ::geometry_msgs::msg::Inertia inertia;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(inertia);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(inertia);
  }

  bool operator==(const InertiaStamped & other) const
  {
    return header == other.header &&
      inertia == other.inertia;
  }
  bool operator!=(const InertiaStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_INERTIASTAMPED_HPP_
