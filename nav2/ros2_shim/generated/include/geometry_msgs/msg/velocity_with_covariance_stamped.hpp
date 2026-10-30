#ifndef GEOMETRY_MSGS_MSG_VELOCITYWITHCOVARIANCESTAMPED_HPP_
#define GEOMETRY_MSGS_MSG_VELOCITYWITHCOVARIANCESTAMPED_HPP_

#include "geometry_msgs/msg/twist_with_covariance.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct VelocityWithCovarianceStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/VelocityWithCovarianceStamped";
  using SharedPtr = std::shared_ptr<VelocityWithCovarianceStamped>;
  using ConstSharedPtr = std::shared_ptr<const VelocityWithCovarianceStamped>;
  using UniquePtr = std::unique_ptr<VelocityWithCovarianceStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _body_frame_id_type = std::string;
  std::string body_frame_id;
  using _reference_frame_id_type = std::string;
  std::string reference_frame_id;
  using _velocity_type = ::geometry_msgs::msg::TwistWithCovariance;
  ::geometry_msgs::msg::TwistWithCovariance velocity;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(body_frame_id);
    ar(reference_frame_id);
    ar(velocity);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(body_frame_id);
    ar(reference_frame_id);
    ar(velocity);
  }

  bool operator==(const VelocityWithCovarianceStamped & other) const
  {
    return header == other.header &&
      body_frame_id == other.body_frame_id &&
      reference_frame_id == other.reference_frame_id &&
      velocity == other.velocity;
  }
  bool operator!=(const VelocityWithCovarianceStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_VELOCITYWITHCOVARIANCESTAMPED_HPP_
