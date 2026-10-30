#include "laser_geometry/laser_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "sensor_msgs/msg/point_field.hpp"
#include "sensor_msgs/point_cloud2_iterator.hpp"

namespace laser_geometry
{

namespace
{

/// A scan that cannot be projected at all. `obstacle_layer` treats
/// `std::runtime_error` as "the message is malformed, drop it" and anything
/// else as a tf failure it can fall back from, so the two cases have to stay
/// distinguishable.
void checkScan(const sensor_msgs::msg::LaserScan & scan)
{
  if (scan.ranges.empty()) {
    throw std::runtime_error(
            "LaserProjection: the scan carries no ranges to project");
  }
  if (scan.range_max <= 0.0f) {
    throw std::runtime_error(
            "LaserProjection: the scan declares a non-positive range_max");
  }
}

/// Roll, pitch, yaw of a rotation, in the same convention tf2 uses.
void rotationToRpy(const tf2::Quaternion & q, double & roll, double & pitch, double & yaw)
{
  const double sinr_cosp = 2.0 * (q.w() * q.x() + q.y() * q.z());
  const double cosr_cosp = 1.0 - 2.0 * (q.x() * q.x() + q.y() * q.y());
  roll = std::atan2(sinr_cosp, cosr_cosp);

  const double sinp = 2.0 * (q.w() * q.y() - q.z() * q.x());
  pitch = std::fabs(sinp) >= 1.0 ? std::copysign(M_PI / 2.0, sinp) : std::asin(sinp);

  const double siny_cosp = 2.0 * (q.w() * q.z() + q.x() * q.y());
  const double cosy_cosp = 1.0 - 2.0 * (q.y() * q.y() + q.z() * q.z());
  yaw = std::atan2(siny_cosp, cosy_cosp);
}

void layoutFields(sensor_msgs::msg::PointCloud2 & cloud, int options)
{
  using sensor_msgs::msg::PointField;
  const bool intensity = (options & channel_option::Intensity) != 0;
  const bool index = (options & channel_option::Index) != 0;
  const bool distance = (options & channel_option::Distance) != 0;
  const bool timestamps = (options & channel_option::Timestamps) != 0;
  const bool viewpoint = (options & channel_option::Viewpoint) != 0;

  cloud.fields.clear();
  uint32_t offset = 0;
  const auto add = [&cloud, &offset](const char * name, uint8_t datatype, uint32_t count) {
      PointField field;
      field.name = name;
      field.offset = offset;
      field.datatype = datatype;
      field.count = count;
      cloud.fields.push_back(field);
      offset += count * sensor_msgs::point_field_size(datatype);
    };

  add("x", PointField::FLOAT32, 1);
  add("y", PointField::FLOAT32, 1);
  add("z", PointField::FLOAT32, 1);
  if (intensity) {
    add("intensity", PointField::FLOAT32, 1);
  }
  if (index) {
    add("index", PointField::UINT32, 1);
  }
  if (distance) {
    add("distance", PointField::FLOAT32, 1);
  }
  if (timestamps) {
    add("timestamps", PointField::FLOAT64, 1);
  }
  if (viewpoint) {
    add("vp_x", PointField::FLOAT32, 1);
    add("vp_y", PointField::FLOAT32, 1);
    add("vp_z", PointField::FLOAT32, 1);
    add("vp_roll", PointField::FLOAT32, 1);
    add("vp_pitch", PointField::FLOAT32, 1);
    add("vp_yaw", PointField::FLOAT32, 1);
  }
  cloud.point_step = offset;
}

/// The one projection routine: `transform` is the scan frame -> output frame
/// transform, and `viewpoint` is where the sensor sits in the output frame.
/// `projectLaser` calls it with the identity for both.
void project(
  const sensor_msgs::msg::LaserScan & scan, const tf2::Transform & transform,
  const tf2::Transform & viewpoint, sensor_msgs::msg::PointCloud2 & cloud,
  double range_cutoff, int channel_options)
{
  checkScan(scan);

  const double max_range =
    range_cutoff > 0.0 ? std::min<double>(scan.range_max, range_cutoff) : scan.range_max;
  const bool has_intensity = !scan.intensities.empty();

  std::vector<int> beams;
  beams.reserve(scan.ranges.size());
  for (size_t i = 0; i < scan.ranges.size(); ++i) {
    const float range = scan.ranges[i];
    if (!std::isfinite(range) || range < scan.range_min || range > max_range) {
      continue;
    }
    beams.push_back(static_cast<int>(i));
  }

  layoutFields(cloud, channel_options);
  cloud.width = static_cast<uint32_t>(beams.size());
  cloud.height = 1;
  cloud.row_step = cloud.point_step * cloud.width;
  cloud.data.assign(static_cast<size_t>(cloud.row_step), 0);
  cloud.is_dense = true;

  const double stamp_seconds =
    static_cast<double>(scan.header.stamp.sec) + 1e-9 * static_cast<double>(scan.header.stamp.nanosec);
  double vp_roll = 0.0;
  double vp_pitch = 0.0;
  double vp_yaw = 0.0;
  rotationToRpy(viewpoint.getRotation(), vp_roll, vp_pitch, vp_yaw);
  const tf2::Vector3 vp_origin = viewpoint.getOrigin();

  sensor_msgs::PointCloud2Iterator<float> iter_x(cloud, "x");
  sensor_msgs::PointCloud2Iterator<float> iter_y(cloud, "y");
  sensor_msgs::PointCloud2Iterator<float> iter_z(cloud, "z");
  sensor_msgs::PointCloud2Iterator<float> iter_intensity(cloud, "intensity");
  sensor_msgs::PointCloud2Iterator<uint32_t> iter_index(cloud, "index");
  sensor_msgs::PointCloud2Iterator<float> iter_distance(cloud, "distance");
  sensor_msgs::PointCloud2Iterator<double> iter_stamp(cloud, "timestamps");
  sensor_msgs::PointCloud2Iterator<float> iter_vp_x(cloud, "vp_x");
  sensor_msgs::PointCloud2Iterator<float> iter_vp_y(cloud, "vp_y");
  sensor_msgs::PointCloud2Iterator<float> iter_vp_z(cloud, "vp_z");
  sensor_msgs::PointCloud2Iterator<float> iter_vp_roll(cloud, "vp_roll");
  sensor_msgs::PointCloud2Iterator<float> iter_vp_pitch(cloud, "vp_pitch");
  sensor_msgs::PointCloud2Iterator<float> iter_vp_yaw(cloud, "vp_yaw");

  for (size_t n = 0; n < beams.size(); ++n) {
    const int beam = beams[n];
    const double range = scan.ranges[beam];
    const double angle = scan.angle_min + beam * scan.angle_increment;

    const tf2::Vector3 point(
      transform.getRotation() * tf2::Vector3(range * std::cos(angle), range * std::sin(angle), 0.0) +
      transform.getOrigin());

    *iter_x = static_cast<float>(point.x());
    *iter_y = static_cast<float>(point.y());
    *iter_z = static_cast<float>(point.z());
    ++iter_x;
    ++iter_y;
    ++iter_z;

    if ((channel_options & channel_option::Intensity) != 0) {
      *iter_intensity = has_intensity ? scan.intensities[beam] : 0.0f;
      ++iter_intensity;
    }
    if ((channel_options & channel_option::Index) != 0) {
      *iter_index = static_cast<uint32_t>(beam);
      ++iter_index;
    }
    if ((channel_options & channel_option::Distance) != 0) {
      *iter_distance = static_cast<float>(std::sqrt(
          point.x() * point.x() + point.y() * point.y() + point.z() * point.z()));
      ++iter_distance;
    }
    if ((channel_options & channel_option::Timestamps) != 0) {
      *iter_stamp = stamp_seconds + beam * scan.time_increment;
      ++iter_stamp;
    }
    if ((channel_options & channel_option::Viewpoint) != 0) {
      *iter_vp_x = static_cast<float>(vp_origin.x());
      *iter_vp_y = static_cast<float>(vp_origin.y());
      *iter_vp_z = static_cast<float>(vp_origin.z());
      *iter_vp_roll = static_cast<float>(vp_roll);
      *iter_vp_pitch = static_cast<float>(vp_pitch);
      *iter_vp_yaw = static_cast<float>(vp_yaw);
      ++iter_vp_x;
      ++iter_vp_y;
      ++iter_vp_z;
      ++iter_vp_roll;
      ++iter_vp_pitch;
      ++iter_vp_yaw;
    }
  }
}

}  // namespace

void LaserProjection::projectLaser(
  const sensor_msgs::msg::LaserScan & scan_in, sensor_msgs::msg::PointCloud2 & cloud_out,
  double range_cutoff, int channel_options) const
{
  const tf2::Transform identity;
  project(scan_in, identity, identity, cloud_out, range_cutoff, channel_options);
}

void LaserProjection::transformLaserScanToPointCloud(
  const std::string & target_frame, const sensor_msgs::msg::LaserScan & scan_in,
  sensor_msgs::msg::PointCloud2 & cloud_out, tf2::BufferCore & tf,
  double range_cutoff, int channel_options) const
{
  checkScan(scan_in);

  const geometry_msgs::msg::TransformStamped stamped = tf.lookupTransform(
    target_frame, scan_in.header.frame_id,
    tf2::timePointFromRosTime(scan_in.header.stamp), tf2::Duration::zero());

  const tf2::Quaternion rotation(
    stamped.transform.rotation.x, stamped.transform.rotation.y,
    stamped.transform.rotation.z, stamped.transform.rotation.w);
  const tf2::Vector3 translation(
    stamped.transform.translation.x, stamped.transform.translation.y,
    stamped.transform.translation.z);
  const tf2::Transform transform(rotation, translation);

  project(scan_in, transform, transform, cloud_out, range_cutoff, channel_options);
  cloud_out.header = scan_in.header;
  cloud_out.header.frame_id = target_frame;
}

}  // namespace laser_geometry
