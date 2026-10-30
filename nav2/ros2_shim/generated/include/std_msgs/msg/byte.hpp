#ifndef STD_MSGS_MSG_BYTE_HPP_
#define STD_MSGS_MSG_BYTE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct Byte
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/Byte";
  using SharedPtr = std::shared_ptr<Byte>;
  using ConstSharedPtr = std::shared_ptr<const Byte>;
  using UniquePtr = std::unique_ptr<Byte>;
  using _data_type = uint8_t;
  uint8_t data = 0;

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

  bool operator==(const Byte & other) const
  {
    return data == other.data;
  }
  bool operator!=(const Byte & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_BYTE_HPP_
