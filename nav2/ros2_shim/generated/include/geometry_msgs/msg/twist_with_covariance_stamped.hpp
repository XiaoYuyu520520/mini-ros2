#ifndef GEOMETRY_MSGS_MSG_TWISTWITHCOVARIANCESTAMPED_HPP_
#define GEOMETRY_MSGS_MSG_TWISTWITHCOVARIANCESTAMPED_HPP_

#include "geometry_msgs/msg/twist_with_covariance.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct TwistWithCovarianceStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/TwistWithCovarianceStamped";
  using SharedPtr = std::shared_ptr<TwistWithCovarianceStamped>;
  using ConstSharedPtr = std::shared_ptr<const TwistWithCovarianceStamped>;
  using UniquePtr = std::unique_ptr<TwistWithCovarianceStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _twist_type = ::geometry_msgs::msg::TwistWithCovariance;
  ::geometry_msgs::msg::TwistWithCovariance twist;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(twist);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(twist);
  }

  bool operator==(const TwistWithCovarianceStamped & other) const
  {
    return header == other.header &&
      twist == other.twist;
  }
  bool operator!=(const TwistWithCovarianceStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_TWISTWITHCOVARIANCESTAMPED_HPP_
