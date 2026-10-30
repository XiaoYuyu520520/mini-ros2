#ifndef SENSOR_MSGS_MSG_IMU_HPP_
#define SENSOR_MSGS_MSG_IMU_HPP_

#include <array>
#include "geometry_msgs/msg/quaternion.hpp"
#include "geometry_msgs/msg/vector3.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct Imu
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/Imu";
  using SharedPtr = std::shared_ptr<Imu>;
  using ConstSharedPtr = std::shared_ptr<const Imu>;
  using UniquePtr = std::unique_ptr<Imu>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _orientation_type = ::geometry_msgs::msg::Quaternion;
  ::geometry_msgs::msg::Quaternion orientation;
  using _orientation_covariance_type = std::array<double, 9>;
  std::array<double, 9> orientation_covariance = {};
  using _angular_velocity_type = ::geometry_msgs::msg::Vector3;
  ::geometry_msgs::msg::Vector3 angular_velocity;
  using _angular_velocity_covariance_type = std::array<double, 9>;
  std::array<double, 9> angular_velocity_covariance = {};
  using _linear_acceleration_type = ::geometry_msgs::msg::Vector3;
  ::geometry_msgs::msg::Vector3 linear_acceleration;
  using _linear_acceleration_covariance_type = std::array<double, 9>;
  std::array<double, 9> linear_acceleration_covariance = {};

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(orientation);
    ar(orientation_covariance);
    ar(angular_velocity);
    ar(angular_velocity_covariance);
    ar(linear_acceleration);
    ar(linear_acceleration_covariance);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(orientation);
    ar(orientation_covariance);
    ar(angular_velocity);
    ar(angular_velocity_covariance);
    ar(linear_acceleration);
    ar(linear_acceleration_covariance);
  }

  bool operator==(const Imu & other) const
  {
    return header == other.header &&
      orientation == other.orientation &&
      orientation_covariance == other.orientation_covariance &&
      angular_velocity == other.angular_velocity &&
      angular_velocity_covariance == other.angular_velocity_covariance &&
      linear_acceleration == other.linear_acceleration &&
      linear_acceleration_covariance == other.linear_acceleration_covariance;
  }
  bool operator!=(const Imu & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_IMU_HPP_
