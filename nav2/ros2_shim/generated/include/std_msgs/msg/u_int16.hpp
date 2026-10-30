#ifndef STD_MSGS_MSG_UINT16_HPP_
#define STD_MSGS_MSG_UINT16_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct UInt16
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/UInt16";
  using SharedPtr = std::shared_ptr<UInt16>;
  using ConstSharedPtr = std::shared_ptr<const UInt16>;
  using UniquePtr = std::unique_ptr<UInt16>;
  using _data_type = uint16_t;
  uint16_t data = 0;

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

  bool operator==(const UInt16 & other) const
  {
    return data == other.data;
  }
  bool operator!=(const UInt16 & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_UINT16_HPP_
