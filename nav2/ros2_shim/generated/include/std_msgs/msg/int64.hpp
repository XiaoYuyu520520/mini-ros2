#ifndef STD_MSGS_MSG_INT64_HPP_
#define STD_MSGS_MSG_INT64_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct Int64
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/Int64";
  using SharedPtr = std::shared_ptr<Int64>;
  using ConstSharedPtr = std::shared_ptr<const Int64>;
  using UniquePtr = std::unique_ptr<Int64>;
  using _data_type = int64_t;
  int64_t data = 0;

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

  bool operator==(const Int64 & other) const
  {
    return data == other.data;
  }
  bool operator!=(const Int64 & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_INT64_HPP_
