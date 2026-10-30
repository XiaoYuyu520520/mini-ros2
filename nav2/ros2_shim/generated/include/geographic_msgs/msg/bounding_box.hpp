#ifndef GEOGRAPHIC_MSGS_MSG_BOUNDINGBOX_HPP_
#define GEOGRAPHIC_MSGS_MSG_BOUNDINGBOX_HPP_

#include "geographic_msgs/msg/geo_point.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geographic_msgs
{
namespace msg
{

struct BoundingBox
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geographic_msgs/msg/BoundingBox";
  using SharedPtr = std::shared_ptr<BoundingBox>;
  using ConstSharedPtr = std::shared_ptr<const BoundingBox>;
  using UniquePtr = std::unique_ptr<BoundingBox>;
  using _min_pt_type = ::geographic_msgs::msg::GeoPoint;
  ::geographic_msgs::msg::GeoPoint min_pt;
  using _max_pt_type = ::geographic_msgs::msg::GeoPoint;
  ::geographic_msgs::msg::GeoPoint max_pt;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(min_pt);
    ar(max_pt);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(min_pt);
    ar(max_pt);
  }

  bool operator==(const BoundingBox & other) const
  {
    return min_pt == other.min_pt &&
      max_pt == other.max_pt;
  }
  bool operator!=(const BoundingBox & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_MSG_BOUNDINGBOX_HPP_
