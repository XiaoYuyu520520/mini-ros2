#ifndef STD_MSGS_MSG_UINT64_HPP_
#define STD_MSGS_MSG_UINT64_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct UInt64
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/UInt64";
  using SharedPtr = std::shared_ptr<UInt64>;
  using ConstSharedPtr = std::shared_ptr<const UInt64>;
  using UniquePtr = std::unique_ptr<UInt64>;
  using _data_type = uint64_t;
  uint64_t data = 0;

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

  bool operator==(const UInt64 & other) const
  {
    return data == other.data;
  }
  bool operator!=(const UInt64 & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_UINT64_HPP_
