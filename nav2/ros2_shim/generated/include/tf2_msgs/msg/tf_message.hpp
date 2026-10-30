#ifndef TF2_MSGS_MSG_TFMESSAGE_HPP_
#define TF2_MSGS_MSG_TFMESSAGE_HPP_

#include "geometry_msgs/msg/transform_stamped.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace tf2_msgs
{
namespace msg
{

struct TFMessage
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "tf2_msgs/msg/TFMessage";
  using SharedPtr = std::shared_ptr<TFMessage>;
  using ConstSharedPtr = std::shared_ptr<const TFMessage>;
  using UniquePtr = std::unique_ptr<TFMessage>;
  using _transforms_type = std::vector<::geometry_msgs::msg::TransformStamped>;
  std::vector<::geometry_msgs::msg::TransformStamped> transforms;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(transforms);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(transforms);
  }

  bool operator==(const TFMessage & other) const
  {
    return transforms == other.transforms;
  }
  bool operator!=(const TFMessage & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace tf2_msgs

#endif  // TF2_MSGS_MSG_TFMESSAGE_HPP_
