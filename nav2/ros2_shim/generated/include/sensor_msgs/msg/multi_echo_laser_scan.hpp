#ifndef SENSOR_MSGS_MSG_MULTIECHOLASERSCAN_HPP_
#define SENSOR_MSGS_MSG_MULTIECHOLASERSCAN_HPP_

#include "sensor_msgs/msg/laser_echo.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct MultiEchoLaserScan
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/MultiEchoLaserScan";
  using SharedPtr = std::shared_ptr<MultiEchoLaserScan>;
  using ConstSharedPtr = std::shared_ptr<const MultiEchoLaserScan>;
  using UniquePtr = std::unique_ptr<MultiEchoLaserScan>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _angle_min_type = float;
  float angle_min = 0.0f;
  using _angle_max_type = float;
  float angle_max = 0.0f;
  using _angle_increment_type = float;
  float angle_increment = 0.0f;
  using _time_increment_type = float;
  float time_increment = 0.0f;
  using _scan_time_type = float;
  float scan_time = 0.0f;
  using _range_min_type = float;
  float range_min = 0.0f;
  using _range_max_type = float;
  float range_max = 0.0f;
  using _ranges_type = std::vector<::sensor_msgs::msg::LaserEcho>;
  std::vector<::sensor_msgs::msg::LaserEcho> ranges;
  using _intensities_type = std::vector<::sensor_msgs::msg::LaserEcho>;
  std::vector<::sensor_msgs::msg::LaserEcho> intensities;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(angle_min);
    ar(angle_max);
    ar(angle_increment);
    ar(time_increment);
    ar(scan_time);
    ar(range_min);
    ar(range_max);
    ar(ranges);
    ar(intensities);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(angle_min);
    ar(angle_max);
    ar(angle_increment);
    ar(time_increment);
    ar(scan_time);
    ar(range_min);
    ar(range_max);
    ar(ranges);
    ar(intensities);
  }

  bool operator==(const MultiEchoLaserScan & other) const
  {
    return header == other.header &&
      angle_min == other.angle_min &&
      angle_max == other.angle_max &&
      angle_increment == other.angle_increment &&
      time_increment == other.time_increment &&
      scan_time == other.scan_time &&
      range_min == other.range_min &&
      range_max == other.range_max &&
      ranges == other.ranges &&
      intensities == other.intensities;
  }
  bool operator!=(const MultiEchoLaserScan & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_MULTIECHOLASERSCAN_HPP_
