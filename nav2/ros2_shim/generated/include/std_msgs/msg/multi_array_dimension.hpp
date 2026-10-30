#ifndef STD_MSGS_MSG_MULTIARRAYDIMENSION_HPP_
#define STD_MSGS_MSG_MULTIARRAYDIMENSION_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct MultiArrayDimension
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/MultiArrayDimension";
  using SharedPtr = std::shared_ptr<MultiArrayDimension>;
  using ConstSharedPtr = std::shared_ptr<const MultiArrayDimension>;
  using UniquePtr = std::unique_ptr<MultiArrayDimension>;
  using _label_type = std::string;
  std::string label;
  using _size_type = uint32_t;
  uint32_t size = 0;
  using _stride_type = uint32_t;
  uint32_t stride = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(label);
    ar(size);
    ar(stride);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(label);
    ar(size);
    ar(stride);
  }

  bool operator==(const MultiArrayDimension & other) const
  {
    return label == other.label &&
      size == other.size &&
      stride == other.stride;
  }
  bool operator!=(const MultiArrayDimension & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_MULTIARRAYDIMENSION_HPP_
