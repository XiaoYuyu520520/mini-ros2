#ifndef STD_MSGS_MSG_UINT32_HPP_
#define STD_MSGS_MSG_UINT32_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct UInt32
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/UInt32";
  using SharedPtr = std::shared_ptr<UInt32>;
  using ConstSharedPtr = std::shared_ptr<const UInt32>;
  using UniquePtr = std::unique_ptr<UInt32>;
  using _data_type = uint32_t;
  uint32_t data = 0;

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

  bool operator==(const UInt32 & other) const
  {
    return data == other.data;
  }
  bool operator!=(const UInt32 & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_UINT32_HPP_
