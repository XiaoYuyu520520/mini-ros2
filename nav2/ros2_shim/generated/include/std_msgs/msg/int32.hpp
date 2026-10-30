#ifndef STD_MSGS_MSG_INT32_HPP_
#define STD_MSGS_MSG_INT32_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct Int32
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/Int32";
  using SharedPtr = std::shared_ptr<Int32>;
  using ConstSharedPtr = std::shared_ptr<const Int32>;
  using UniquePtr = std::unique_ptr<Int32>;
  using _data_type = int32_t;
  int32_t data = 0;

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

  bool operator==(const Int32 & other) const
  {
    return data == other.data;
  }
  bool operator!=(const Int32 & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_INT32_HPP_
