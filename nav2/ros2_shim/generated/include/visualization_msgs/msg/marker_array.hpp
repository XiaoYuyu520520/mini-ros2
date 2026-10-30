#ifndef VISUALIZATION_MSGS_MSG_MARKERARRAY_HPP_
#define VISUALIZATION_MSGS_MSG_MARKERARRAY_HPP_

#include <vector>
#include "visualization_msgs/msg/marker.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace visualization_msgs
{
namespace msg
{

struct MarkerArray
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "visualization_msgs/msg/MarkerArray";
  using SharedPtr = std::shared_ptr<MarkerArray>;
  using ConstSharedPtr = std::shared_ptr<const MarkerArray>;
  using UniquePtr = std::unique_ptr<MarkerArray>;
  using _markers_type = std::vector<::visualization_msgs::msg::Marker>;
  std::vector<::visualization_msgs::msg::Marker> markers;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(markers);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(markers);
  }

  bool operator==(const MarkerArray & other) const
  {
    return markers == other.markers;
  }
  bool operator!=(const MarkerArray & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace visualization_msgs

#endif  // VISUALIZATION_MSGS_MSG_MARKERARRAY_HPP_
