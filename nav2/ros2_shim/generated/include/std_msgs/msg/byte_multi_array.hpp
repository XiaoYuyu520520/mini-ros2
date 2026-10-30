#ifndef STD_MSGS_MSG_BYTEMULTIARRAY_HPP_
#define STD_MSGS_MSG_BYTEMULTIARRAY_HPP_

#include "std_msgs/msg/multi_array_layout.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct ByteMultiArray
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/ByteMultiArray";
  using SharedPtr = std::shared_ptr<ByteMultiArray>;
  using ConstSharedPtr = std::shared_ptr<const ByteMultiArray>;
  using UniquePtr = std::unique_ptr<ByteMultiArray>;
  using _layout_type = ::std_msgs::msg::MultiArrayLayout;
  ::std_msgs::msg::MultiArrayLayout layout;
  using _data_type = std::vector<uint8_t>;
  std::vector<uint8_t> data;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(layout);
    ar(data);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(layout);
    ar(data);
  }

  bool operator==(const ByteMultiArray & other) const
  {
    return layout == other.layout &&
      data == other.data;
  }
  bool operator!=(const ByteMultiArray & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_BYTEMULTIARRAY_HPP_
