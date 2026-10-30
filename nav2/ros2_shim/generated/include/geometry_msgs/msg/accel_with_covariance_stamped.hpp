#ifndef GEOMETRY_MSGS_MSG_ACCELWITHCOVARIANCESTAMPED_HPP_
#define GEOMETRY_MSGS_MSG_ACCELWITHCOVARIANCESTAMPED_HPP_

#include "geometry_msgs/msg/accel_with_covariance.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct AccelWithCovarianceStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/AccelWithCovarianceStamped";
  using SharedPtr = std::shared_ptr<AccelWithCovarianceStamped>;
  using ConstSharedPtr = std::shared_ptr<const AccelWithCovarianceStamped>;
  using UniquePtr = std::unique_ptr<AccelWithCovarianceStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _accel_type = ::geometry_msgs::msg::AccelWithCovariance;
  ::geometry_msgs::msg::AccelWithCovariance accel;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(accel);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(accel);
  }

  bool operator==(const AccelWithCovarianceStamped & other) const
  {
    return header == other.header &&
      accel == other.accel;
  }
  bool operator!=(const AccelWithCovarianceStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_ACCELWITHCOVARIANCESTAMPED_HPP_
