#ifndef GEOGRAPHIC_MSGS_MSG_GEOPOSEWITHCOVARIANCE_HPP_
#define GEOGRAPHIC_MSGS_MSG_GEOPOSEWITHCOVARIANCE_HPP_

#include <array>
#include "geographic_msgs/msg/geo_pose.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geographic_msgs
{
namespace msg
{

struct GeoPoseWithCovariance
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geographic_msgs/msg/GeoPoseWithCovariance";
  using SharedPtr = std::shared_ptr<GeoPoseWithCovariance>;
  using ConstSharedPtr = std::shared_ptr<const GeoPoseWithCovariance>;
  using UniquePtr = std::unique_ptr<GeoPoseWithCovariance>;
  using _pose_type = ::geographic_msgs::msg::GeoPose;
  ::geographic_msgs::msg::GeoPose pose;
  using _covariance_type = std::array<double, 36>;
  std::array<double, 36> covariance = {};

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(pose);
    ar(covariance);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(pose);
    ar(covariance);
  }

  bool operator==(const GeoPoseWithCovariance & other) const
  {
    return pose == other.pose &&
      covariance == other.covariance;
  }
  bool operator!=(const GeoPoseWithCovariance & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_MSG_GEOPOSEWITHCOVARIANCE_HPP_
