#ifndef STD_MSGS_MSG_BOOL_HPP_
#define STD_MSGS_MSG_BOOL_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct Bool
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/Bool";
  using SharedPtr = std::shared_ptr<Bool>;
  using ConstSharedPtr = std::shared_ptr<const Bool>;
  using UniquePtr = std::unique_ptr<Bool>;
  using _data_type = bool;
  bool data = false;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(data);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(data);
  }

  bool operator==(const Bool & other) const
  {
    return data == other.data;
  }
  bool operator!=(const Bool & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_BOOL_HPP_
