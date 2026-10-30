#ifndef SENSOR_MSGS_MSG_POINTCLOUD2_HPP_
#define SENSOR_MSGS_MSG_POINTCLOUD2_HPP_

#include "sensor_msgs/msg/point_field.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct PointCloud2
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/PointCloud2";
  using SharedPtr = std::shared_ptr<PointCloud2>;
  using ConstSharedPtr = std::shared_ptr<const PointCloud2>;
  using UniquePtr = std::unique_ptr<PointCloud2>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _height_type = uint32_t;
  uint32_t height = 0;
  using _width_type = uint32_t;
  uint32_t width = 0;
  using _fields_type = std::vector<::sensor_msgs::msg::PointField>;
  std::vector<::sensor_msgs::msg::PointField> fields;
  using _is_bigendian_type = bool;
  bool is_bigendian = false;
  using _point_step_type = uint32_t;
  uint32_t point_step = 0;
  using _row_step_type = uint32_t;
  uint32_t row_step = 0;
  using _data_type = std::vector<uint8_t>;
  std::vector<uint8_t> data;
  using _is_dense_type = bool;
  bool is_dense = false;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(height);
    ar(width);
    ar(fields);
    ar(is_bigendian);
    ar(point_step);
    ar(row_step);
    ar(data);
    ar(is_dense);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(height);
    ar(width);
    ar(fields);
    ar(is_bigendian);
    ar(point_step);
    ar(row_step);
    ar(data);
    ar(is_dense);
  }

  bool operator==(const PointCloud2 & other) const
  {
    return header == other.header &&
      height == other.height &&
      width == other.width &&
      fields == other.fields &&
      is_bigendian == other.is_bigendian &&
      point_step == other.point_step &&
      row_step == other.row_step &&
      data == other.data &&
      is_dense == other.is_dense;
  }
  bool operator!=(const PointCloud2 & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_POINTCLOUD2_HPP_
