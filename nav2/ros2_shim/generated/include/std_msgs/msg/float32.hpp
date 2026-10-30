#ifndef STD_MSGS_MSG_FLOAT32_HPP_
#define STD_MSGS_MSG_FLOAT32_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct Float32
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/Float32";
  using SharedPtr = std::shared_ptr<Float32>;
  using ConstSharedPtr = std::shared_ptr<const Float32>;
  using UniquePtr = std::unique_ptr<Float32>;
  using _data_type = float;
  float data = 0.0f;

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

  bool operator==(const Float32 & other) const
  {
    return data == other.data;
  }
  bool operator!=(const Float32 & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_FLOAT32_HPP_
