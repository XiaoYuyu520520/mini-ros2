#ifndef STD_MSGS_MSG_HEADER_HPP_
#define STD_MSGS_MSG_HEADER_HPP_

#include "builtin_interfaces/msg/time.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct Header
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/Header";
  using SharedPtr = std::shared_ptr<Header>;
  using ConstSharedPtr = std::shared_ptr<const Header>;
  using UniquePtr = std::unique_ptr<Header>;
  using _stamp_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time stamp;
  using _frame_id_type = std::string;
  std::string frame_id;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(stamp);
    ar(frame_id);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(stamp);
    ar(frame_id);
  }

  bool operator==(const Header & other) const
  {
    return stamp == other.stamp &&
      frame_id == other.frame_id;
  }
  bool operator!=(const Header & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_HEADER_HPP_
