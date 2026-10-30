#ifndef STD_MSGS_MSG_INT8_HPP_
#define STD_MSGS_MSG_INT8_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct Int8
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/Int8";
  using SharedPtr = std::shared_ptr<Int8>;
  using ConstSharedPtr = std::shared_ptr<const Int8>;
  using UniquePtr = std::unique_ptr<Int8>;
  using _data_type = int8_t;
  int8_t data = 0;

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

  bool operator==(const Int8 & other) const
  {
    return data == other.data;
  }
  bool operator!=(const Int8 & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_INT8_HPP_
