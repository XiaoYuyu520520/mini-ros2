#ifndef GEOMETRY_MSGS_MSG_POSEWITHCOVARIANCESTAMPED_HPP_
#define GEOMETRY_MSGS_MSG_POSEWITHCOVARIANCESTAMPED_HPP_

#include "geometry_msgs/msg/pose_with_covariance.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct PoseWithCovarianceStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/PoseWithCovarianceStamped";
  using SharedPtr = std::shared_ptr<PoseWithCovarianceStamped>;
  using ConstSharedPtr = std::shared_ptr<const PoseWithCovarianceStamped>;
  using UniquePtr = std::unique_ptr<PoseWithCovarianceStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _pose_type = ::geometry_msgs::msg::PoseWithCovariance;
  ::geometry_msgs::msg::PoseWithCovariance pose;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(pose);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(pose);
  }

  bool operator==(const PoseWithCovarianceStamped & other) const
  {
    return header == other.header &&
      pose == other.pose;
  }
  bool operator!=(const PoseWithCovarianceStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_POSEWITHCOVARIANCESTAMPED_HPP_
