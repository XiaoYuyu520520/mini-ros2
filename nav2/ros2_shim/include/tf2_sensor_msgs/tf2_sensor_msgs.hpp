#ifndef TF2_SENSOR_MSGS__TF2_SENSOR_MSGS_HPP_
#define TF2_SENSOR_MSGS__TF2_SENSOR_MSGS_HPP_

#include <cstring>
#include <string>

#include "geometry_msgs/msg/transform_stamped.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "sensor_msgs/point_cloud2_iterator.hpp"
#include "tf2/LinearMath/Transform.hpp"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"

namespace tf2
{

inline int find_field_offset(
  const sensor_msgs::msg::PointCloud2 & cloud, const std::string & name, uint8_t & datatype)
{
  for (const auto & field : cloud.fields) {
    if (field.name == name) {
      datatype = field.datatype;
      return static_cast<int>(field.offset);
    }
  }
  return -1;
}

inline void doTransform(
  const sensor_msgs::msg::PointCloud2 & in, sensor_msgs::msg::PointCloud2 & out,
  const geometry_msgs::msg::TransformStamped & transform)
{
  out = in;
  out.header.frame_id = transform.header.frame_id;

  tf2::Transform tr;
  fromMsg(transform.transform, tr);

  uint8_t x_type = 0, y_type = 0, z_type = 0;
  const int x_off = find_field_offset(in, "x", x_type);
  const int y_off = find_field_offset(in, "y", y_type);
  const int z_off = find_field_offset(in, "z", z_type);
  if (x_off < 0 || y_off < 0 || z_off < 0) {
    return;
  }

  const size_t count = static_cast<size_t>(in.width) * in.height;
  for (size_t i = 0; i < count; ++i) {
    uint8_t * base = out.data.data() + i * in.point_step;
    float x = 0, y = 0, z = 0;
    std::memcpy(&x, base + x_off, sizeof(float));
    std::memcpy(&y, base + y_off, sizeof(float));
    std::memcpy(&z, base + z_off, sizeof(float));
    const tf2::Vector3 p = tr(tf2::Vector3(x, y, z));
    const float ox = static_cast<float>(p.x());
    const float oy = static_cast<float>(p.y());
    const float oz = static_cast<float>(p.z());
    std::memcpy(base + x_off, &ox, sizeof(float));
    std::memcpy(base + y_off, &oy, sizeof(float));
    std::memcpy(base + z_off, &oz, sizeof(float));
  }
}

inline void doTransform(
  const sensor_msgs::msg::LaserScan & in, sensor_msgs::msg::LaserScan & out,
  const geometry_msgs::msg::TransformStamped & transform)
{
  out = in;
  out.header.frame_id = transform.header.frame_id;
}

}  // namespace tf2

#endif  // TF2_SENSOR_MSGS__TF2_SENSOR_MSGS_HPP_
