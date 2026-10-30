#ifndef STD_MSGS_MSG_MULTIARRAYLAYOUT_HPP_
#define STD_MSGS_MSG_MULTIARRAYLAYOUT_HPP_

#include "std_msgs/msg/multi_array_dimension.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct MultiArrayLayout
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/MultiArrayLayout";
  using SharedPtr = std::shared_ptr<MultiArrayLayout>;
  using ConstSharedPtr = std::shared_ptr<const MultiArrayLayout>;
  using UniquePtr = std::unique_ptr<MultiArrayLayout>;
  using _dim_type = std::vector<::std_msgs::msg::MultiArrayDimension>;
  std::vector<::std_msgs::msg::MultiArrayDimension> dim;
  using _data_offset_type = uint32_t;
  uint32_t data_offset = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(dim);
    ar(data_offset);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(dim);
    ar(data_offset);
  }

  bool operator==(const MultiArrayLayout & other) const
  {
    return dim == other.dim &&
      data_offset == other.data_offset;
  }
  bool operator!=(const MultiArrayLayout & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_MULTIARRAYLAYOUT_HPP_
