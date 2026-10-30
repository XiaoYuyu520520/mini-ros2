#include "ros2_bridge/type_registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <type_traits>

#include "ros2_bridge/cdr_archive.hpp"
#include "ros2_bridge/field_parse.hpp"
#include "ros2_bridge/transcode.hpp"
#include "ros2_shim/parameter_support.hpp"

#include "action_msgs/msg/goal_status_array.hpp"
#include "geometry_msgs/msg/pose_array.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "geometry_msgs/msg/pose_with_covariance_stamped.hpp"
#include "geometry_msgs/msg/polygon_stamped.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "geometry_msgs/msg/twist_stamped.hpp"
#include "nav2_msgs/msg/costmap.hpp"
#include "nav_msgs/msg/map_meta_data.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "nav_msgs/msg/path.hpp"
#include "rosgraph_msgs/msg/clock.hpp"
#include "sensor_msgs/msg/camera_info.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "sensor_msgs/msg/imu.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/string.hpp"
#include "tf2_msgs/msg/tf_message.hpp"
#include "visualization_msgs/msg/marker_array.hpp"
// The rest of what the Windows side of a SLAM + nav2 stack puts on the graph.
// Without a codec here `ros2 topic echo` can only print a byte count for the
// topic, which is where most of nav2's own diagnostics live.
#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "dwb_msgs/msg/local_plan_evaluation.hpp"
#include "geometry_msgs/msg/point_stamped.hpp"
#include "geometry_msgs/msg/polygon.hpp"
#include "map_msgs/msg/occupancy_grid_update.hpp"
#include "nav2_msgs/msg/behavior_tree_log.hpp"
#include "nav2_msgs/msg/collision_monitor_state.hpp"
#include "nav2_msgs/msg/costmap_filter_info.hpp"
#include "nav2_msgs/msg/costmap_update.hpp"
#include "nav2_msgs/msg/critics_stats.hpp"
#include "nav2_msgs/msg/particle_cloud.hpp"
#include "nav2_msgs/msg/speed_limit.hpp"
#include "nav2_msgs/msg/tracking_feedback.hpp"
#include "nav2_msgs/msg/voxel_grid.hpp"
#include "nav_msgs/msg/trajectory.hpp"
#include "rcl_interfaces/msg/log.hpp"
#include "rcl_interfaces/msg/parameter_event.hpp"
#include "std_msgs/msg/empty.hpp"
#include "visualization_msgs/msg/marker.hpp"

namespace ros2_bridge
{
namespace
{

std::string stamp(const builtin_interfaces::msg::Time & time)
{
  char buffer[64] = {0};
  std::snprintf(
    buffer, sizeof(buffer), "%d.%09u", time.sec, static_cast<unsigned>(time.nanosec));
  return buffer;
}

std::string header(const std_msgs::msg::Header & value)
{
  std::ostringstream out;
  out << "  stamp: " << stamp(value.stamp) << "\n"
      << "  frame_id: \"" << value.frame_id << "\"\n";
  return out.str();
}

template<class Values>
std::string summarize(const Values & values)
{
  std::ostringstream out;
  if (values.empty()) {
    out << "[]";
    return out.str();
  }
  auto bounds = std::minmax_element(values.begin(), values.end());
  out << "[" << values.size() << " values, min " << static_cast<double>(*bounds.first)
      << ", max " << static_cast<double>(*bounds.second) << "]";
  return out.str();
}

// The first `limit` points of a footprint / point list, then how many were
// left out: enough to see the shape without pasting a few hundred lines.
template<class PointList>
void printPointList(std::ostringstream & out, const PointList & points, std::size_t limit)
{
  const std::size_t shown = std::min(points.size(), limit);
  for (std::size_t i = 0; i < shown; ++i) {
    out << "  - {x: " << points[i].x << ", y: " << points[i].y
        << ", z: " << points[i].z << "}\n";
  }
  if (points.size() > shown) {
    out << "  ... " << (points.size() - shown) << " more\n";
  }
}

// A map or a map update at a glance: `nav_msgs/OccupancyGrid` cell convention
// (-1 unknown, 0 free, 100 occupied).
template<class Cells>
std::string summarizeCells(const Cells & cells)
{
  std::size_t occupied = 0;
  std::size_t free = 0;
  std::size_t unknown = 0;
  for (const auto cell : cells) {
    const int value = static_cast<int>(cell);
    if (value < 0) {
      ++unknown;
    } else if (value >= 100) {
      ++occupied;
    } else if (value == 0) {
      ++free;
    }
  }
  std::ostringstream out;
  out << "[" << cells.size() << " cells, " << occupied << " occupied, "
      << free << " free, " << unknown << " unknown]";
  return out.str();
}

// `nav2_msgs/VoxelGrid.data` is a bitfield, so the interesting number is how
// many voxels are set rather than a min/max of the words.
template<class Words>
std::size_t countSetBits(const Words & words)
{
  std::size_t bits = 0;
  for (const auto word : words) {
    unsigned long long value = 0;
    for (std::size_t i = 0; i < sizeof(word); ++i) {
      value |= static_cast<unsigned long long>(static_cast<unsigned char>(word >> (8 * i)))
        << (8 * i);
    }
    while (value != 0) {
      value &= value - 1;
      ++bits;
    }
  }
  return bits;
}

const char * collisionActionName(uint8_t action_type)
{
  switch (action_type) {
    case nav2_msgs::msg::CollisionMonitorState::DO_NOTHING: return "do_nothing";
    case nav2_msgs::msg::CollisionMonitorState::STOP: return "stop";
    case nav2_msgs::msg::CollisionMonitorState::SLOWDOWN: return "slowdown";
    case nav2_msgs::msg::CollisionMonitorState::APPROACH: return "approach";
    case nav2_msgs::msg::CollisionMonitorState::LIMIT: return "limit";
    default: return "unknown";
  }
}

// `action_msgs/msg/GoalStatus.status`. The numbers are what the topic carries
// and what a client compares against; the names are what a reader needs, since
// "status: 4" is the one thing nobody can look up at a glance.
const char * goalStatusName(int8_t status)
{
  switch (status) {
    case action_msgs::msg::GoalStatus::STATUS_UNKNOWN: return "UNKNOWN";
    case action_msgs::msg::GoalStatus::STATUS_ACCEPTED: return "ACCEPTED";
    case action_msgs::msg::GoalStatus::STATUS_EXECUTING: return "EXECUTING";
    case action_msgs::msg::GoalStatus::STATUS_CANCELING: return "CANCELING";
    case action_msgs::msg::GoalStatus::STATUS_SUCCEEDED: return "SUCCEEDED";
    case action_msgs::msg::GoalStatus::STATUS_CANCELED: return "CANCELED";
    case action_msgs::msg::GoalStatus::STATUS_ABORTED: return "ABORTED";
    default: return "unknown";
  }
}

const char * diagnosticLevelName(uint8_t level)
{
  switch (level) {
    case diagnostic_msgs::msg::DiagnosticStatus::OK: return "OK";
    case diagnostic_msgs::msg::DiagnosticStatus::WARN: return "WARN";
    case diagnostic_msgs::msg::DiagnosticStatus::ERROR: return "ERROR";
    case diagnostic_msgs::msg::DiagnosticStatus::STALE: return "STALE";
    default: return "UNKNOWN";
  }
}

const char * logLevelName(uint8_t level)
{
  switch (level) {
    case rcl_interfaces::msg::Log::DEBUG: return "DEBUG";
    case rcl_interfaces::msg::Log::INFO: return "INFO";
    case rcl_interfaces::msg::Log::WARN: return "WARN";
    case rcl_interfaces::msg::Log::ERROR: return "ERROR";
    case rcl_interfaces::msg::Log::FATAL: return "FATAL";
    default: return "UNKNOWN";
  }
}

const char * markerTypeName(int32_t type)
{
  switch (type) {
    case visualization_msgs::msg::Marker::ARROW: return "ARROW";
    case visualization_msgs::msg::Marker::CUBE: return "CUBE";
    case visualization_msgs::msg::Marker::SPHERE: return "SPHERE";
    case visualization_msgs::msg::Marker::CYLINDER: return "CYLINDER";
    case visualization_msgs::msg::Marker::LINE_STRIP: return "LINE_STRIP";
    case visualization_msgs::msg::Marker::LINE_LIST: return "LINE_LIST";
    case visualization_msgs::msg::Marker::CUBE_LIST: return "CUBE_LIST";
    case visualization_msgs::msg::Marker::SPHERE_LIST: return "SPHERE_LIST";
    case visualization_msgs::msg::Marker::POINTS: return "POINTS";
    case visualization_msgs::msg::Marker::TEXT_VIEW_FACING: return "TEXT_VIEW_FACING";
    case visualization_msgs::msg::Marker::MESH_RESOURCE: return "MESH_RESOURCE";
    case visualization_msgs::msg::Marker::TRIANGLE_LIST: return "TRIANGLE_LIST";
    case visualization_msgs::msg::Marker::ARROW_STRIP: return "ARROW_STRIP";
    default: return "UNKNOWN";
  }
}

const char * markerActionName(int32_t action)
{
  switch (action) {
    case visualization_msgs::msg::Marker::ADD: return "ADD";
    case visualization_msgs::msg::Marker::DELETE: return "DELETE";
    case visualization_msgs::msg::Marker::DELETEALL: return "DELETEALL";
    default: return "UNKNOWN";
  }
}

// One entry of an `rcl_interfaces/ParameterEvent`, the message behind
// `/parameter_events`. Shares the spelling with `ros2 param` on purpose.
void printParameter(std::ostringstream & out, const rcl_interfaces::msg::Parameter & parameter)
{
  const rcl_interfaces::msg::ParameterValue & value = parameter.value;
  out << "  - " << parameter.name << ": " << ros2_shim::parameter_type_name(value.type);
  switch (value.type) {
    case 1: out << " " << (value.bool_value ? "true" : "false"); break;
    case 2: out << " " << value.integer_value; break;
    case 3: out << " " << value.double_value; break;
    case 4: out << " \"" << value.string_value << "\""; break;
    case 5: out << " [" << value.byte_array_value.size() << " bytes]"; break;
    case 6: out << " [" << value.bool_array_value.size() << " values]"; break;
    case 7: out << " [" << value.integer_array_value.size() << " values]"; break;
    case 8: out << " [" << value.double_array_value.size() << " values]"; break;
    case 9: out << " [" << value.string_array_value.size() << " values]"; break;
    default: break;
  }
  out << "\n";
}

void printParameters(
  std::ostringstream & out, const char * label,
  const std::vector<rcl_interfaces::msg::Parameter> & parameters)
{
  if (parameters.empty()) {
    return;
  }
  out << label << ": [" << parameters.size() << "]\n";
  const std::size_t shown = std::min<std::size_t>(parameters.size(), 5);
  for (std::size_t i = 0; i < shown; ++i) {
    printParameter(out, parameters[i]);
  }
  if (parameters.size() > shown) {
    out << "  ... " << (parameters.size() - shown) << " more\n";
  }
}

template<class Message>
std::string describe(const Message & message)
{
  std::ostringstream out;
  if constexpr (std::is_same_v<Message, sensor_msgs::msg::LaserScan>) {
    out << "header:\n" << header(message.header)
        << "angle_min: " << message.angle_min << "\n"
        << "angle_max: " << message.angle_max << "\n"
        << "angle_increment: " << message.angle_increment << "\n"
        << "range_min: " << message.range_min << "\n"
        << "range_max: " << message.range_max << "\n"
        << "ranges: " << summarize(message.ranges) << "\n";
  } else if constexpr (std::is_same_v<Message, nav_msgs::msg::Odometry>) {
    out << "header:\n" << header(message.header)
        << "child_frame_id: \"" << message.child_frame_id << "\"\n"
        << "pose.pose.position: {x: " << message.pose.pose.position.x
        << ", y: " << message.pose.pose.position.y
        << ", z: " << message.pose.pose.position.z << "}\n"
        << "pose.pose.orientation: {x: " << message.pose.pose.orientation.x
        << ", y: " << message.pose.pose.orientation.y
        << ", z: " << message.pose.pose.orientation.z
        << ", w: " << message.pose.pose.orientation.w << "}\n"
        << "twist.twist.linear: {x: " << message.twist.twist.linear.x
        << ", y: " << message.twist.twist.linear.y
        << ", z: " << message.twist.twist.linear.z << "}\n"
        << "twist.twist.angular: {x: " << message.twist.twist.angular.x
        << ", y: " << message.twist.twist.angular.y
        << ", z: " << message.twist.twist.angular.z << "}\n";
  } else if constexpr (std::is_same_v<Message, nav_msgs::msg::OccupancyGrid>) {
    std::size_t occupied = 0;
    for (int8_t cell : message.data) {
      if (cell >= 100) {
        ++occupied;
      }
    }
    out << "header:\n" << header(message.header)
        << "info: {resolution: " << message.info.resolution
        << ", width: " << message.info.width
        << ", height: " << message.info.height
        << ", origin: {x: " << message.info.origin.position.x
        << ", y: " << message.info.origin.position.y << "}}\n"
        << "data: [" << message.data.size() << " cells, " << occupied
        << " occupied]\n";
  } else if constexpr (std::is_same_v<Message, nav_msgs::msg::Path>) {
    out << "header:\n" << header(message.header)
        << "poses: [" << message.poses.size() << " waypoints]\n";
    if (!message.poses.empty()) {
      const auto & first = message.poses.front().pose.position;
      const auto & last = message.poses.back().pose.position;
      out << "  first: {x: " << first.x << ", y: " << first.y << "}\n"
          << "  last: {x: " << last.x << ", y: " << last.y << "}\n";
    }
  } else if constexpr (std::is_same_v<Message, tf2_msgs::msg::TFMessage>) {
    out << "transforms: [" << message.transforms.size() << "]\n";
    for (const auto & transform : message.transforms) {
      out << "  - header: {stamp: " << stamp(transform.header.stamp)
          << ", frame_id: \"" << transform.header.frame_id << "\"}\n"
          << "    child_frame_id: \"" << transform.child_frame_id << "\"\n"
          << "    translation: {x: " << transform.transform.translation.x
          << ", y: " << transform.transform.translation.y
          << ", z: " << transform.transform.translation.z << "}\n"
          << "    rotation: {x: " << transform.transform.rotation.x
          << ", y: " << transform.transform.rotation.y
          << ", z: " << transform.transform.rotation.z
          << ", w: " << transform.transform.rotation.w << "}\n";
    }
  } else if constexpr (std::is_same_v<Message, geometry_msgs::msg::Twist>) {
    out << "linear: {x: " << message.linear.x << ", y: " << message.linear.y
        << ", z: " << message.linear.z << "}\n"
        << "angular: {x: " << message.angular.x << ", y: " << message.angular.y
        << ", z: " << message.angular.z << "}\n";
  } else if constexpr (std::is_same_v<Message, geometry_msgs::msg::TwistStamped>) {
    out << "header:\n" << header(message.header)
        << "twist.linear: {x: " << message.twist.linear.x
        << ", y: " << message.twist.linear.y << "}\n"
        << "twist.angular: {z: " << message.twist.angular.z << "}\n";
  } else if constexpr (std::is_same_v<Message, geometry_msgs::msg::PoseStamped>) {
    out << "header:\n" << header(message.header)
        << "pose.position: {x: " << message.pose.position.x
        << ", y: " << message.pose.position.y << "}\n"
        << "pose.orientation: {z: " << message.pose.orientation.z
        << ", w: " << message.pose.orientation.w << "}\n";
  } else if constexpr (std::is_same_v<Message, geometry_msgs::msg::PoseWithCovarianceStamped>) {
    out << "header:\n" << header(message.header)
        << "pose.pose.position: {x: " << message.pose.pose.position.x
        << ", y: " << message.pose.pose.position.y << "}\n"
        << "pose.pose.orientation: {z: " << message.pose.pose.orientation.z
        << ", w: " << message.pose.pose.orientation.w << "}\n";
  } else if constexpr (std::is_same_v<Message, geometry_msgs::msg::PoseArray>) {
    out << "header:\n" << header(message.header)
        << "poses: [" << message.poses.size() << "]\n";
  } else if constexpr (std::is_same_v<Message, geometry_msgs::msg::PolygonStamped>) {
    out << "header:\n" << header(message.header)
        << "polygon.points: [" << message.polygon.points.size() << "]\n";
  } else if constexpr (std::is_same_v<Message, rosgraph_msgs::msg::Clock>) {
    out << "clock: " << stamp(message.clock) << "\n";
  } else if constexpr (std::is_same_v<Message, std_msgs::msg::String>) {
    out << "data: \"" << message.data << "\"\n";
  } else if constexpr (std::is_same_v<Message, std_msgs::msg::Bool>) {
    out << "data: " << (message.data ? "true" : "false") << "\n";
  } else if constexpr (std::is_same_v<Message, sensor_msgs::msg::Imu>) {
    out << "header:\n" << header(message.header)
        << "linear_acceleration: {x: " << message.linear_acceleration.x
        << ", y: " << message.linear_acceleration.y << "}\n"
        << "angular_velocity: {z: " << message.angular_velocity.z << "}\n";
  } else if constexpr (std::is_same_v<Message, sensor_msgs::msg::PointCloud2>) {
    out << "header:\n" << header(message.header)
        << "width: " << message.width << "\n"
        << "height: " << message.height << "\n"
        << "point_step: " << message.point_step << "\n"
        << "data: [" << message.data.size() << " bytes]\n";
  } else if constexpr (std::is_same_v<Message, nav_msgs::msg::MapMetaData>) {
    out << "resolution: " << message.resolution << "\n"
        << "width: " << message.width << "\n"
        << "height: " << message.height << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::Costmap>) {
    out << "header:\n" << header(message.header)
        << "metadata: {resolution: " << message.metadata.resolution
        << ", size_x: " << message.metadata.size_x
        << ", size_y: " << message.metadata.size_y << "}\n"
        << "data: [" << message.data.size() << " cells]\n";
  } else if constexpr (std::is_same_v<Message, std_msgs::msg::Empty>) {
    // No fields at all: the CLI prints just the `---` separator, which is what
    // a native `ros2 topic echo` does on an Empty topic.
  } else if constexpr (std::is_same_v<Message, geometry_msgs::msg::PointStamped>) {
    out << "header:\n" << header(message.header)
        << "point: {x: " << message.point.x << ", y: " << message.point.y
        << ", z: " << message.point.z << "}\n";
  } else if constexpr (std::is_same_v<Message, geometry_msgs::msg::Polygon>) {
    out << "points: [" << message.points.size() << "]\n";
    printPointList(out, message.points, 4);
  } else if constexpr (std::is_same_v<Message, nav_msgs::msg::Trajectory>) {
    out << "header:\n" << header(message.header)
        << "points: [" << message.points.size() << "]\n";
    const std::size_t shown = std::min<std::size_t>(message.points.size(), 3);
    for (std::size_t i = 0; i < shown; ++i) {
      const auto & point = message.points[i];
      out << "  - stamp: " << stamp(point.header.stamp)
          << ", position: {x: " << point.pose.position.x
          << ", y: " << point.pose.position.y << "}"
          << ", velocity: {linear.x: " << point.velocity.linear.x
          << ", angular.z: " << point.velocity.angular.z << "}\n";
    }
  } else if constexpr (std::is_same_v<Message, map_msgs::msg::OccupancyGridUpdate>) {
    out << "header:\n" << header(message.header)
        << "x: " << message.x << "\n"
        << "y: " << message.y << "\n"
        << "width: " << message.width << "\n"
        << "height: " << message.height << "\n"
        << "data: " << summarizeCells(message.data) << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::CostmapUpdate>) {
    out << "header:\n" << header(message.header)
        << "x: " << message.x << "\n"
        << "y: " << message.y << "\n"
        << "size_x: " << message.size_x << "\n"
        << "size_y: " << message.size_y << "\n"
        << "data: [" << message.data.size() << " cells]\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::CostmapFilterInfo>) {
    out << "header:\n" << header(message.header)
        << "type: " << static_cast<int>(message.type) << "\n"
        << "filter_mask_topic: \"" << message.filter_mask_topic << "\"\n"
        << "base: " << message.base << "\n"
        << "multiplier: " << message.multiplier << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::SpeedLimit>) {
    out << "header:\n" << header(message.header)
        << "percentage: " << (message.percentage ? "true" : "false") << "\n"
        << "speed_limit: " << message.speed_limit << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::VoxelGrid>) {
    out << "header:\n" << header(message.header)
        << "size: {x: " << message.size_x << ", y: " << message.size_y
        << ", z: " << message.size_z << "}\n"
        << "resolutions: {x: " << message.resolutions.x << ", y: " << message.resolutions.y
        << ", z: " << message.resolutions.z << "}\n"
        << "origin: {x: " << message.origin.x << ", y: " << message.origin.y
        << ", z: " << message.origin.z << "}\n"
        << "data: [" << message.data.size() << " words, "
        << countSetBits(message.data) << " voxels set]\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::ParticleCloud>) {
    out << "header:\n" << header(message.header)
        << "particles: [" << message.particles.size() << "]\n";
    if (!message.particles.empty()) {
      const auto best = std::max_element(
        message.particles.begin(), message.particles.end(),
        [](const nav2_msgs::msg::Particle & a, const nav2_msgs::msg::Particle & b) {
          return a.weight < b.weight;
        });
      out << "  best: {x: " << best->pose.position.x << ", y: " << best->pose.position.y
          << ", weight: " << best->weight << "}\n";
    }
  } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::BehaviorTreeLog>) {
    out << "timestamp: " << stamp(message.timestamp) << "\n"
        << "event_log: [" << message.event_log.size() << "]\n";
    const std::size_t shown = std::min<std::size_t>(message.event_log.size(), 5);
    for (std::size_t i = 0; i < shown; ++i) {
      const auto & event = message.event_log[i];
      out << "  - " << stamp(event.timestamp) << " " << event.node_name
          << " uid " << event.uid << ": " << event.previous_status
          << " -> " << event.current_status << "\n";
    }
  } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::CriticsStats>) {
    out << "stamp: " << stamp(message.stamp) << "\n"
        << "critics: [" << message.critics.size() << "]\n";
    for (std::size_t i = 0; i < message.critics.size(); ++i) {
      out << "  - " << message.critics[i] << ":";
      if (i < message.costs_sum.size()) {
        out << " cost " << message.costs_sum[i];
      }
      if (i < message.changed.size()) {
        out << (message.changed[i] ? " (changed)" : " (same)");
      }
      out << "\n";
    }
  } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::TrackingFeedback>) {
    out << "header:\n" << header(message.header)
        << "position_tracking_error: " << message.position_tracking_error << "\n"
        << "heading_tracking_error: " << message.heading_tracking_error << "\n"
        << "current_path_index: " << message.current_path_index << "\n"
        << "robot_pose: {x: " << message.robot_pose.pose.position.x
        << ", y: " << message.robot_pose.pose.position.y << "}\n"
        << "distance_to_goal: " << message.distance_to_goal << "\n"
        << "speed: " << message.speed << "\n"
        << "remaining_path_length: " << message.remaining_path_length << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::CollisionMonitorState>) {
    out << "action_type: " << static_cast<int>(message.action_type)
        << " (" << collisionActionName(message.action_type) << ")\n"
        << "polygon_name: \"" << message.polygon_name << "\"\n";
  } else if constexpr (std::is_same_v<Message, dwb_msgs::msg::LocalPlanEvaluation>) {
    out << "header:\n" << header(message.header)
        << "twists: [" << message.twists.size() << "]\n"
        << "best_index: " << message.best_index << "\n"
        << "worst_index: " << message.worst_index << "\n";
    if (message.best_index < message.twists.size()) {
      const auto & best = message.twists[message.best_index];
      out << "  best: total " << best.total << ", poses " << best.traj.poses.size()
          << ", scores [" << best.scores.size() << "]\n";
    }
  } else if constexpr (std::is_same_v<Message, diagnostic_msgs::msg::DiagnosticArray>) {
    out << "header:\n" << header(message.header)
        << "status: [" << message.status.size() << "]\n";
    const std::size_t shown = std::min<std::size_t>(message.status.size(), 5);
    for (std::size_t i = 0; i < shown; ++i) {
      const auto & status = message.status[i];
      out << "  - " << diagnosticLevelName(status.level) << " " << status.name
          << ": " << status.message << "\n";
      const std::size_t values = std::min<std::size_t>(status.values.size(), 3);
      for (std::size_t v = 0; v < values; ++v) {
        out << "      " << status.values[v].key << "=" << status.values[v].value << "\n";
      }
    }
  } else if constexpr (std::is_same_v<Message, rcl_interfaces::msg::Log>) {
    out << "stamp: " << stamp(message.stamp) << "\n"
        << "level: " << static_cast<int>(message.level)
        << " (" << logLevelName(message.level) << ")\n"
        << "name: \"" << message.name << "\"\n"
        << "msg: \"" << message.msg << "\"\n"
        << "file: \"" << message.file << "\" line " << message.line
        << " function: \"" << message.function << "\"\n";
  } else if constexpr (std::is_same_v<Message, rcl_interfaces::msg::ParameterEvent>) {
    out << "stamp: " << stamp(message.stamp) << "\n"
        << "node: \"" << message.node << "\"\n";
    printParameters(out, "new_parameters", message.new_parameters);
    printParameters(out, "changed_parameters", message.changed_parameters);
    printParameters(out, "deleted_parameters", message.deleted_parameters);
  } else if constexpr (std::is_same_v<Message, visualization_msgs::msg::Marker>) {
    out << "header:\n" << header(message.header)
        << "ns: \"" << message.ns << "\" id: " << message.id << "\n"
        << "type: " << markerTypeName(message.type)
        << ", action: " << markerActionName(message.action) << "\n"
        << "pose: {x: " << message.pose.position.x << ", y: " << message.pose.position.y
        << ", z: " << message.pose.position.z << "}\n"
        << "scale: {x: " << message.scale.x << ", y: " << message.scale.y
        << ", z: " << message.scale.z << "}\n"
        << "color: {r: " << message.color.r << ", g: " << message.color.g
        << ", b: " << message.color.b << ", a: " << message.color.a << "}\n"
        << "points: [" << message.points.size() << "]\n";
    if (!message.text.empty()) {
      out << "text: \"" << message.text << "\"\n";
    }
  } else if constexpr (std::is_same_v<Message, sensor_msgs::msg::Image>) {
    // A camera image: the pixels are the payload, so what a caller needs from a
    // `topic echo` is the shape and the encoding, not three million bytes.
    out << "header:\n" << header(message.header)
        << "size: " << message.width << "x" << message.height << "\n"
        << "encoding: \"" << message.encoding << "\"\n"
        << "is_bigendian: " << static_cast<int>(message.is_bigendian) << "\n"
        << "step: " << message.step << "\n"
        << "data: " << summarize(message.data) << "\n";
  } else if constexpr (std::is_same_v<Message, sensor_msgs::msg::CameraInfo>) {
    out << "header:\n" << header(message.header)
        << "size: " << message.width << "x" << message.height << "\n"
        << "distortion_model: \"" << message.distortion_model << "\"\n"
        << "d: " << summarize(message.d) << "\n"
        << "k: " << summarize(message.k) << "\n"
        << "r: " << summarize(message.r) << "\n"
        << "p: " << summarize(message.p) << "\n"
        << "binning: " << message.binning_x << "x" << message.binning_y << "\n"
        << "roi: {x_offset: " << message.roi.x_offset
        << ", y_offset: " << message.roi.y_offset
        << ", height: " << message.roi.height << ", width: " << message.roi.width
        << ", do_rectify: " << (message.roi.do_rectify ? "true" : "false") << "}\n";
  } else if constexpr (std::is_same_v<Message, action_msgs::msg::GoalStatusArray>) {
    // `<action>/_action/status`: every goal this action server still knows
    // about and the state each one is in. This is the topic a client watches to
    // know that a goal it sent was accepted, is being worked on, and ended -
    // `waypoint_cycle` drives its next waypoint off it - so the useful answer
    // is the state by name and the goal it belongs to.
    out << "status_list: [" << message.status_list.size() << "]\n";
    for (const auto & status : message.status_list) {
      out << "  - goal_id: ";
      for (const uint8_t byte : status.goal_info.goal_id.uuid) {
        char hex[4] = {0};
        std::snprintf(hex, sizeof(hex), "%02x", byte);
        out << hex;
      }
      out << "\n    stamp: " << stamp(status.goal_info.stamp)
          << "\n    status: " << static_cast<int>(status.status)
          << " (" << goalStatusName(status.status) << ")\n";
    }
  } else {
    out << "(no pretty printer registered for this type)\n";
  }
  return out.str();
}

template<class Message, class = void>
struct has_header : std::false_type {};

template<class Message>
struct has_header<Message, std::void_t<decltype(Message::header)>> : std::true_type {};

bool applyHeader(
  std_msgs::msg::Header & header, const std::string & path, const std::string & value,
  bool & handled)
{
  handled = true;
  if (path == "header.frame_id") {
    return setScalar(header.frame_id, value);
  }
  if (path == "header.stamp.sec") {
    return setScalar(header.stamp.sec, value);
  }
  if (path == "header.stamp.nanosec") {
    return setScalar(header.stamp.nanosec, value);
  }
  handled = false;
  return true;
}

#define ASSIGN(EXPR) \
  if (path == #EXPR) { \
    handled = true; \
    if (!setScalar(message.EXPR, value)) { \
      error = "invalid value for " #EXPR ": '" + value + "'"; \
      return false; \
    } \
  }

#define ASSIGN_SEQ(EXPR) \
  if (path == #EXPR) { \
    handled = true; \
    if (!setSequence(message.EXPR, value)) { \
      error = "invalid list for " #EXPR ": '" + value + "'"; \
      return false; \
    } \
  }

// One element of a message sequence, `poses[2].pose.position.x`: the element is
// filled through this same type-by-type dispatch, so a `PoseStamped` under
// `poses[2].` reads exactly the paths it reads when it is the whole message.
// The sequence grows to hold the index, so the elements may be written in any
// order and `poses[2]....` on its own still means three of them. A sequence of
// *scalars* keeps its comma separated spelling (`setSequence` above): a
// `float[]` has one, a `PoseStamped[]` has not.
#define ASSIGN_SEQ_ELEMENT(EXPR) \
  if (indexed.indexed && indexed.prefix == #EXPR) { \
    handled = true; \
    if (indexed.index >= message.EXPR.size()) { \
      message.EXPR.resize(indexed.index + 1); \
    } \
    const std::vector<FieldAssign> one{FieldAssign{indexed.rest, value}}; \
    if (!applyFields(message.EXPR[indexed.index], one, error)) { \
      return false; \
    } \
  }

// The same sequence written without an index - `poses=...`, which is what a
// caller who has not read this file would try. `[]` is an empty list and is
// taken; anything else says how to spell an element, because "no settable field
// 'poses'" reads like a typo.
#define ASSIGN_SEQ_HINT(EXPR) \
  if (path == #EXPR) { \
    handled = true; \
    if (value.empty()) { \
      message.EXPR.clear(); \
    } else { \
      error = "'" #EXPR "' carries messages, one per element: write '" #EXPR "[0].' and then " \
        "a field of that element"; \
      return false; \
    } \
  }

template<class Message>
bool applyFields(Message & message, const std::vector<FieldAssign> & fields, std::string & error)
{
  for (const auto & field : fields) {
    const std::string & path = field.path;
    const std::string & value = field.value;
    const IndexedPath indexed = splitIndex(path);
    bool handled = false;

    if constexpr (std::is_same_v<Message, geometry_msgs::msg::Twist>) {
      ASSIGN(linear.x) else ASSIGN(linear.y) else ASSIGN(linear.z)
      else ASSIGN(angular.x) else ASSIGN(angular.y) else ASSIGN(angular.z)
    } else if constexpr (std::is_same_v<Message, geometry_msgs::msg::TwistStamped>) {
      ASSIGN(twist.linear.x) else ASSIGN(twist.linear.y) else ASSIGN(twist.linear.z)
      else ASSIGN(twist.angular.x) else ASSIGN(twist.angular.y) else ASSIGN(twist.angular.z)
    } else if constexpr (std::is_same_v<Message, geometry_msgs::msg::PoseStamped>) {
      ASSIGN(pose.position.x) else ASSIGN(pose.position.y) else ASSIGN(pose.position.z)
      else ASSIGN(pose.orientation.x) else ASSIGN(pose.orientation.y)
      else ASSIGN(pose.orientation.z) else ASSIGN(pose.orientation.w)
    } else if constexpr (std::is_same_v<Message, geometry_msgs::msg::PoseWithCovarianceStamped>) {
      ASSIGN(pose.pose.position.x) else ASSIGN(pose.pose.position.y)
      else ASSIGN(pose.pose.position.z)
      else ASSIGN(pose.pose.orientation.x) else ASSIGN(pose.pose.orientation.y)
      else ASSIGN(pose.pose.orientation.z) else ASSIGN(pose.pose.orientation.w)
      else ASSIGN_SEQ(pose.covariance)
    } else if constexpr (std::is_same_v<Message, nav_msgs::msg::Odometry>) {
      ASSIGN(child_frame_id)
      else ASSIGN(pose.pose.position.x) else ASSIGN(pose.pose.position.y)
      else ASSIGN(pose.pose.orientation.z) else ASSIGN(pose.pose.orientation.w)
      else ASSIGN(twist.twist.linear.x) else ASSIGN(twist.twist.linear.y)
      else ASSIGN(twist.twist.angular.z)
    } else if constexpr (std::is_same_v<Message, sensor_msgs::msg::LaserScan>) {
      ASSIGN(angle_min) else ASSIGN(angle_max) else ASSIGN(angle_increment)
      else ASSIGN(time_increment) else ASSIGN(scan_time)
      else ASSIGN(range_min) else ASSIGN(range_max)
      else ASSIGN_SEQ(ranges) else ASSIGN_SEQ(intensities)
    } else if constexpr (std::is_same_v<Message, nav_msgs::msg::OccupancyGrid>) {
      ASSIGN(info.resolution) else ASSIGN(info.width) else ASSIGN(info.height)
      else ASSIGN(info.origin.position.x) else ASSIGN(info.origin.position.y)
      else ASSIGN(info.origin.orientation.z) else ASSIGN(info.origin.orientation.w)
      else ASSIGN_SEQ(data)
    } else if constexpr (std::is_same_v<Message, nav_msgs::msg::Path>) {
      // `poses` is the `geometry_msgs/PoseStamped[]` a path is made of, and the
      // reason a plan can be written from the command line at all: it has no
      // comma separated spelling (see ASSIGN_SEQ_ELEMENT).
      ASSIGN_SEQ_ELEMENT(poses)
      ASSIGN_SEQ_HINT(poses)
    } else if constexpr (std::is_same_v<Message, nav_msgs::msg::MapMetaData>) {
      ASSIGN(resolution) else ASSIGN(width) else ASSIGN(height)
      else ASSIGN(origin.position.x) else ASSIGN(origin.position.y)
    } else if constexpr (std::is_same_v<Message, std_msgs::msg::String>) {
      ASSIGN(data)
    } else if constexpr (std::is_same_v<Message, std_msgs::msg::Bool>) {
      ASSIGN(data)
    } else if constexpr (std::is_same_v<Message, rosgraph_msgs::msg::Clock>) {
      ASSIGN(clock.sec) else ASSIGN(clock.nanosec)
    } else if constexpr (std::is_same_v<Message, sensor_msgs::msg::Imu>) {
      ASSIGN(linear_acceleration.x) else ASSIGN(linear_acceleration.y)
      else ASSIGN(linear_acceleration.z)
      else ASSIGN(angular_velocity.x) else ASSIGN(angular_velocity.y)
      else ASSIGN(angular_velocity.z)
      else ASSIGN(orientation.x) else ASSIGN(orientation.y)
      else ASSIGN(orientation.z) else ASSIGN(orientation.w)
    } else if constexpr (std::is_same_v<Message, sensor_msgs::msg::PointCloud2>) {
      ASSIGN(width) else ASSIGN(height) else ASSIGN(point_step) else ASSIGN(row_step)
      else ASSIGN_SEQ(data)
    } else if constexpr (std::is_same_v<Message, tf2_msgs::msg::TFMessage>) {
      // Convenience form: one transform per message, yaw instead of a
      // quaternion, so `ros2 topic pub /tf ... frame_id=odom child_frame_id=
      // base_link x=1 y=2 theta=0.5` works.
      if (path == "frame_id" || path == "child_frame_id" || path == "x" || path == "y" ||
        path == "z" || path == "theta" || path == "yaw")
      {
        handled = true;
        if (message.transforms.empty()) {
          geometry_msgs::msg::TransformStamped transform;
          transform.header.frame_id = "map";
          transform.transform.rotation.w = 1.0;
          message.transforms.push_back(transform);
        }
        auto & transform = message.transforms.front();
        if (path == "frame_id") {
          if (!setScalar(transform.header.frame_id, value)) {
            error = "invalid frame_id";
            return false;
          }
        } else if (path == "child_frame_id") {
          if (!setScalar(transform.child_frame_id, value)) {
            error = "invalid child_frame_id";
            return false;
          }
        } else if (path == "x") {
          if (!setScalar(transform.transform.translation.x, value)) {
            error = "invalid x";
            return false;
          }
        } else if (path == "y") {
          if (!setScalar(transform.transform.translation.y, value)) {
            error = "invalid y";
            return false;
          }
        } else if (path == "z") {
          if (!setScalar(transform.transform.translation.z, value)) {
            error = "invalid z";
            return false;
          }
        } else {
          double yaw = 0.0;
          if (!setScalar(yaw, value)) {
            error = "invalid theta";
            return false;
          }
          transform.transform.rotation.z = std::sin(yaw / 2.0);
          transform.transform.rotation.w = std::cos(yaw / 2.0);
        }
      }
    } else if constexpr (std::is_same_v<Message, geometry_msgs::msg::PointStamped>) {
      ASSIGN(point.x) else ASSIGN(point.y) else ASSIGN(point.z)
    } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::SpeedLimit>) {
      ASSIGN(percentage) else ASSIGN(speed_limit)
    } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::CostmapUpdate>) {
      ASSIGN(x) else ASSIGN(y) else ASSIGN(size_x) else ASSIGN(size_y)
      else ASSIGN_SEQ(data)
    } else if constexpr (std::is_same_v<Message, map_msgs::msg::OccupancyGridUpdate>) {
      ASSIGN(x) else ASSIGN(y) else ASSIGN(width) else ASSIGN(height)
      else ASSIGN_SEQ(data)
    } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::CostmapFilterInfo>) {
      ASSIGN(type) else ASSIGN(filter_mask_topic) else ASSIGN(base) else ASSIGN(multiplier)
    } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::VoxelGrid>) {
      ASSIGN(size_x) else ASSIGN(size_y) else ASSIGN(size_z)
      else ASSIGN(resolutions.x) else ASSIGN(resolutions.y) else ASSIGN(resolutions.z)
      else ASSIGN(origin.x) else ASSIGN(origin.y) else ASSIGN(origin.z)
    } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::TrackingFeedback>) {
      ASSIGN(position_tracking_error) else ASSIGN(heading_tracking_error)
      else ASSIGN(current_path_index) else ASSIGN(distance_to_goal)
      else ASSIGN(speed) else ASSIGN(remaining_path_length)
      else ASSIGN(robot_pose.pose.position.x) else ASSIGN(robot_pose.pose.position.y)
    } else if constexpr (std::is_same_v<Message, nav2_msgs::msg::CollisionMonitorState>) {
      ASSIGN(action_type) else ASSIGN(polygon_name)
    } else if constexpr (std::is_same_v<Message, rcl_interfaces::msg::Log>) {
      ASSIGN(level) else ASSIGN(name) else ASSIGN(msg)
      else ASSIGN(file) else ASSIGN(line) else ASSIGN(function)
    } else if constexpr (std::is_same_v<Message, rcl_interfaces::msg::ParameterEvent>) {
      ASSIGN(node)
    } else if constexpr (std::is_same_v<Message, visualization_msgs::msg::Marker>) {
      ASSIGN(ns) else ASSIGN(id) else ASSIGN(type) else ASSIGN(action)
      else ASSIGN(pose.position.x) else ASSIGN(pose.position.y) else ASSIGN(pose.position.z)
      else ASSIGN(pose.orientation.x) else ASSIGN(pose.orientation.y)
      else ASSIGN(pose.orientation.z) else ASSIGN(pose.orientation.w)
      else ASSIGN(scale.x) else ASSIGN(scale.y) else ASSIGN(scale.z)
      else ASSIGN(color.r) else ASSIGN(color.g) else ASSIGN(color.b) else ASSIGN(color.a)
      else ASSIGN(text)
    } else if constexpr (std::is_same_v<Message, sensor_msgs::msg::Image>) {
      ASSIGN(height) else ASSIGN(width) else ASSIGN(encoding) else ASSIGN(is_bigendian)
      else ASSIGN(step) else ASSIGN_SEQ(data)
    } else if constexpr (std::is_same_v<Message, sensor_msgs::msg::CameraInfo>) {
      // The four camera matrices are fixed size; `setSequence` on a
      // `std::array` takes the same comma separated spelling and refuses a list
      // that does not fit.
      ASSIGN(height) else ASSIGN(width) else ASSIGN(distortion_model)
      else ASSIGN_SEQ(d) else ASSIGN_SEQ(k) else ASSIGN_SEQ(r) else ASSIGN_SEQ(p)
      else ASSIGN(binning_x) else ASSIGN(binning_y)
      else ASSIGN(roi.x_offset) else ASSIGN(roi.y_offset)
      else ASSIGN(roi.height) else ASSIGN(roi.width) else ASSIGN(roi.do_rectify)
    }

    if constexpr (has_header<Message>::value) {
      if (!handled) {
        bool header_handled = false;
        if (!applyHeader(message.header, path, value, header_handled)) {
          error = "invalid value for " + path + ": '" + value + "'";
          return false;
        }
        handled = header_handled;
      }
    }

    if (!handled) {
      error = "type '" + std::string(Message::ros2_message_type) +
        "' has no settable field '" + path + "'";
      return false;
    }
  }
  return true;
}

#undef ASSIGN
#undef ASSIGN_SEQ
#undef ASSIGN_SEQ_ELEMENT
#undef ASSIGN_SEQ_HINT

template<class Message>
Codec makeCodec(const char * type)
{
  Codec codec;
  codec.type = type;
  codec.to_cdr = &shimToCdr<Message>;
  codec.to_shim = &cdrToShim<Message>;
  codec.describe = [](const Bytes & shim) {
      Message message;
      if (!ros2_shim::deserialize(shim.data(), shim.size(), message)) {
        return std::string("(undecodable payload)");
      }
      return describe(message);
    };
  codec.build = [](const std::vector<FieldAssign> & fields, Bytes & shim, std::string & error) {
      Message message{};
      if (!applyFields(message, fields, error)) {
        return false;
      }
      shim = ros2_shim::serialize(message);
      return true;
    };
  return codec;
}

const std::map<std::string, Codec> & registry()
{
  static const std::map<std::string, Codec> table = {
    {"sensor_msgs/msg/LaserScan", makeCodec<sensor_msgs::msg::LaserScan>("sensor_msgs/msg/LaserScan")},
    {"sensor_msgs/msg/PointCloud2", makeCodec<sensor_msgs::msg::PointCloud2>("sensor_msgs/msg/PointCloud2")},
    {"sensor_msgs/msg/Imu", makeCodec<sensor_msgs::msg::Imu>("sensor_msgs/msg/Imu")},
    // The camera pair. Without these two every image topic on the robot is a
    // byte count on this side, and SLAM's vision-only plugins have no way in
    // (04-gaps.md section B).
    {"sensor_msgs/msg/Image", makeCodec<sensor_msgs::msg::Image>("sensor_msgs/msg/Image")},
    {"sensor_msgs/msg/CameraInfo",
      makeCodec<sensor_msgs::msg::CameraInfo>("sensor_msgs/msg/CameraInfo")},
    {"nav_msgs/msg/Odometry", makeCodec<nav_msgs::msg::Odometry>("nav_msgs/msg/Odometry")},
    {"nav_msgs/msg/OccupancyGrid", makeCodec<nav_msgs::msg::OccupancyGrid>("nav_msgs/msg/OccupancyGrid")},
    {"nav_msgs/msg/Path", makeCodec<nav_msgs::msg::Path>("nav_msgs/msg/Path")},
    {"nav_msgs/msg/MapMetaData", makeCodec<nav_msgs::msg::MapMetaData>("nav_msgs/msg/MapMetaData")},
    {"geometry_msgs/msg/Twist", makeCodec<geometry_msgs::msg::Twist>("geometry_msgs/msg/Twist")},
    {"geometry_msgs/msg/TwistStamped", makeCodec<geometry_msgs::msg::TwistStamped>("geometry_msgs/msg/TwistStamped")},
    {"geometry_msgs/msg/PoseStamped", makeCodec<geometry_msgs::msg::PoseStamped>("geometry_msgs/msg/PoseStamped")},
    {"geometry_msgs/msg/PoseWithCovarianceStamped",
      makeCodec<geometry_msgs::msg::PoseWithCovarianceStamped>("geometry_msgs/msg/PoseWithCovarianceStamped")},
    {"geometry_msgs/msg/PoseArray", makeCodec<geometry_msgs::msg::PoseArray>("geometry_msgs/msg/PoseArray")},
    {"geometry_msgs/msg/PolygonStamped", makeCodec<geometry_msgs::msg::PolygonStamped>("geometry_msgs/msg/PolygonStamped")},
    {"tf2_msgs/msg/TFMessage", makeCodec<tf2_msgs::msg::TFMessage>("tf2_msgs/msg/TFMessage")},
    {"std_msgs/msg/String", makeCodec<std_msgs::msg::String>("std_msgs/msg/String")},
    {"std_msgs/msg/Bool", makeCodec<std_msgs::msg::Bool>("std_msgs/msg/Bool")},
    {"rosgraph_msgs/msg/Clock", makeCodec<rosgraph_msgs::msg::Clock>("rosgraph_msgs/msg/Clock")},
    {"nav2_msgs/msg/Costmap", makeCodec<nav2_msgs::msg::Costmap>("nav2_msgs/msg/Costmap")},
    {"visualization_msgs/msg/MarkerArray",
      makeCodec<visualization_msgs::msg::MarkerArray>("visualization_msgs/msg/MarkerArray")},
    {"geometry_msgs/msg/PointStamped",
      makeCodec<geometry_msgs::msg::PointStamped>("geometry_msgs/msg/PointStamped")},
    {"geometry_msgs/msg/Polygon", makeCodec<geometry_msgs::msg::Polygon>("geometry_msgs/msg/Polygon")},
    {"nav_msgs/msg/Trajectory",
      makeCodec<nav_msgs::msg::Trajectory>("nav_msgs/msg/Trajectory")},
    {"std_msgs/msg/Empty", makeCodec<std_msgs::msg::Empty>("std_msgs/msg/Empty")},
    {"visualization_msgs/msg/Marker",
      makeCodec<visualization_msgs::msg::Marker>("visualization_msgs/msg/Marker")},
    {"diagnostic_msgs/msg/DiagnosticArray",
      makeCodec<diagnostic_msgs::msg::DiagnosticArray>("diagnostic_msgs/msg/DiagnosticArray")},
    {"rcl_interfaces/msg/Log", makeCodec<rcl_interfaces::msg::Log>("rcl_interfaces/msg/Log")},
    {"rcl_interfaces/msg/ParameterEvent",
      makeCodec<rcl_interfaces::msg::ParameterEvent>("rcl_interfaces/msg/ParameterEvent")},
    {"map_msgs/msg/OccupancyGridUpdate",
      makeCodec<map_msgs::msg::OccupancyGridUpdate>("map_msgs/msg/OccupancyGridUpdate")},
    {"nav2_msgs/msg/CostmapUpdate",
      makeCodec<nav2_msgs::msg::CostmapUpdate>("nav2_msgs/msg/CostmapUpdate")},
    {"nav2_msgs/msg/CostmapFilterInfo",
      makeCodec<nav2_msgs::msg::CostmapFilterInfo>("nav2_msgs/msg/CostmapFilterInfo")},
    {"nav2_msgs/msg/SpeedLimit",
      makeCodec<nav2_msgs::msg::SpeedLimit>("nav2_msgs/msg/SpeedLimit")},
    {"nav2_msgs/msg/VoxelGrid", makeCodec<nav2_msgs::msg::VoxelGrid>("nav2_msgs/msg/VoxelGrid")},
    {"nav2_msgs/msg/ParticleCloud",
      makeCodec<nav2_msgs::msg::ParticleCloud>("nav2_msgs/msg/ParticleCloud")},
    {"nav2_msgs/msg/BehaviorTreeLog",
      makeCodec<nav2_msgs::msg::BehaviorTreeLog>("nav2_msgs/msg/BehaviorTreeLog")},
    {"nav2_msgs/msg/CriticsStats",
      makeCodec<nav2_msgs::msg::CriticsStats>("nav2_msgs/msg/CriticsStats")},
    {"nav2_msgs/msg/TrackingFeedback",
      makeCodec<nav2_msgs::msg::TrackingFeedback>("nav2_msgs/msg/TrackingFeedback")},
    {"nav2_msgs/msg/CollisionMonitorState",
      makeCodec<nav2_msgs::msg::CollisionMonitorState>("nav2_msgs/msg/CollisionMonitorState")},
    {"dwb_msgs/msg/LocalPlanEvaluation",
      makeCodec<dwb_msgs::msg::LocalPlanEvaluation>("dwb_msgs/msg/LocalPlanEvaluation")},
    // `<action>/_action/status`, which the action servers publish and
    // `waypoint_cycle` subscribes to. Without a codec here it is a byte count
    // in `ros2 topic echo`, and - because the bridge transcodes only what it
    // has a codec for - a topic that cannot be mirrored to the peer either.
    {"action_msgs/msg/GoalStatusArray",
      makeCodec<action_msgs::msg::GoalStatusArray>("action_msgs/msg/GoalStatusArray")},
  };
  return table;
}

// Topics spelled with an aliased package name, e.g. `example_interfaces/Int64`
// or `std_msgs/String` (no explicit kind), are normalised before lookup.
std::string normalise(const std::string & type_name)
{
  if (type_name.empty()) {
    return type_name;
  }
  const std::size_t first_separator = type_name.find('/');
  if (first_separator == std::string::npos) {
    return type_name;
  }
  const std::size_t second_separator = type_name.find('/', first_separator + 1);
  if (second_separator == std::string::npos) {
    return type_name.substr(0, first_separator) + "/msg/" + type_name.substr(first_separator + 1);
  }
  return type_name;
}

}  // namespace

const Codec * find_codec(const std::string & type_name)
{
  const std::string key = normalise(type_name);
  const auto & table = registry();
  const auto it = table.find(key);
  return it == table.end() ? nullptr : &it->second;
}

bool is_supported(const std::string & type_name)
{
  return find_codec(type_name) != nullptr;
}

std::vector<std::string> supported_types()
{
  std::vector<std::string> types;
  types.reserve(registry().size());
  for (const auto & entry : registry()) {
    types.push_back(entry.first);
  }
  return types;
}

bool shim_to_cdr(const std::string & type_name, const Bytes & shim, Bytes & cdr)
{
  cdr.clear();
  const Codec * codec = find_codec(type_name);
  if (codec == nullptr) {
    return false;
  }
  try {
    return codec->to_cdr(shim, cdr);
  } catch (const std::exception &) {
    cdr.clear();
    return false;
  }
}

bool cdr_to_shim(const std::string & type_name, const Bytes & cdr, Bytes & shim)
{
  shim.clear();
  const Codec * codec = find_codec(type_name);
  if (codec == nullptr) {
    return false;
  }
  try {
    return codec->to_shim(cdr, shim);
  } catch (const std::exception &) {
    shim.clear();
    return false;
  }
}

bool build_shim(
  const std::string & type_name, const std::vector<FieldAssign> & fields, Bytes & shim,
  std::string * error)
{
  shim.clear();
  const Codec * codec = find_codec(type_name);
  if (codec == nullptr) {
    if (error != nullptr) {
      *error = "unknown type '" + type_name + "'";
    }
    return false;
  }
  if (!codec->build) {
    if (error != nullptr) {
      *error = "type '" + type_name + "' cannot be built from the command line";
    }
    return false;
  }
  std::string local_error;
  try {
    if (codec->build(fields, shim, local_error)) {
      return true;
    }
  } catch (const std::exception & exception) {
    local_error = exception.what();
  }
  shim.clear();
  if (error != nullptr) {
    *error = local_error.empty() ? std::string("could not build message") : local_error;
  }
  return false;
}

std::string describe_shim(const std::string & type_name, const Bytes & shim)
{
  const Codec * codec = find_codec(type_name);
  if (codec == nullptr || !codec->describe) {
    return {};
  }
  try {
    return codec->describe(shim);
  } catch (const std::exception &) {
    return "(undecodable payload)";
  }
}

}  // namespace ros2_bridge