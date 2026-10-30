#ifndef LASER_GEOMETRY__LASER_GEOMETRY_HPP_
#define LASER_GEOMETRY__LASER_GEOMETRY_HPP_

// Projection of a `sensor_msgs::msg::LaserScan` into a
// `sensor_msgs::msg::PointCloud2`.
//
// nav2_costmap_2d's `obstacle_layer` and `voxel_layer` turn every scan into a
// point cloud before they buffer it, and they do it through this class. Only
// the two entry points those layers call are provided; the pcl-templated
// overloads and the `LaserProjection` members upstream keeps for them are not,
// because nothing in the port names them.

#include <string>

#include "sensor_msgs/msg/laser_scan.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "tf2/buffer_core.hpp"
#include "tf2/time.hpp"

namespace laser_geometry
{

namespace channel_option
{
enum ChannelOption
{
  None = 0x00,
  Intensity = 0x01,
  Index = 0x02,
  Distance = 0x04,
  Timestamps = 0x08,
  Viewpoint = 0x10,
  Default = Intensity | Distance
};
}  // namespace channel_option

class LaserProjection
{
public:
  LaserProjection() = default;

  /// Project a scan into a cloud expressed in the scan's own frame.
  void projectLaser(
    const sensor_msgs::msg::LaserScan & scan_in, sensor_msgs::msg::PointCloud2 & cloud_out,
    double range_cutoff = -1.0, int channel_options = channel_option::Default) const;

  /// Project a scan and move every point into `target_frame`. A beam the
  /// transform cannot be found for is dropped, not fatal: only a malformed
  /// scan throws `std::runtime_error`, and only a tf failure throws
  /// `tf2::TransformException`.
  void transformLaserScanToPointCloud(
    const std::string & target_frame, const sensor_msgs::msg::LaserScan & scan_in,
    sensor_msgs::msg::PointCloud2 & cloud_out, tf2::BufferCore & tf,
    double range_cutoff = -1.0, int channel_options = channel_option::Default) const;
};

}  // namespace laser_geometry

#endif  // LASER_GEOMETRY__LASER_GEOMETRY_HPP_
