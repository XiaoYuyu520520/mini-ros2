#ifndef SENSOR_MSGS_MSG_POINTCLOUD_HPP_
#define SENSOR_MSGS_MSG_POINTCLOUD_HPP_

#include "geometry_msgs/msg/point32.hpp"
#include "sensor_msgs/msg/channel_float32.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct PointCloud
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/PointCloud";
  using SharedPtr = std::shared_ptr<PointCloud>;
  using ConstSharedPtr = std::shared_ptr<const PointCloud>;
  using UniquePtr = std::unique_ptr<PointCloud>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _points_type = std::vector<::geometry_msgs::msg::Point32>;
  std::vector<::geometry_msgs::msg::Point32> points;
  using _channels_type = std::vector<::sensor_msgs::msg::ChannelFloat32>;
  std::vector<::sensor_msgs::msg::ChannelFloat32> channels;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(points);
    ar(channels);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(points);
    ar(channels);
  }

  bool operator==(const PointCloud & other) const
  {
    return header == other.header &&
      points == other.points &&
      channels == other.channels;
  }
  bool operator!=(const PointCloud & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_POINTCLOUD_HPP_
