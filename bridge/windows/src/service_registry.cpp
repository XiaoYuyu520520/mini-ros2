#include "ros2_bridge/service_registry.hpp"

#include <map>
#include <sstream>
#include <type_traits>

#include "ros2_bridge/field_parse.hpp"
#include "ros2_bridge/transcode.hpp"
#include "ros2_shim/serialization.hpp"

#include "lifecycle_msgs/srv/change_state.hpp"
#include "lifecycle_msgs/srv/get_available_transitions.hpp"
#include "lifecycle_msgs/srv/get_state.hpp"
#include "lifecycle_msgs/msg/state.hpp"
#include "lifecycle_msgs/msg/transition.hpp"
#include "geometry_msgs/msg/pose2_d.hpp"
#include "geometry_msgs/msg/pose_with_covariance_stamped.hpp"
#include "nav2_msgs/msg/circle_object.hpp"
#include "nav2_msgs/msg/costmap.hpp"
#include "nav2_msgs/msg/exclusion_zone_description.hpp"
#include "nav2_msgs/msg/polygon_object.hpp"
#include "nav2_msgs/srv/add_exclusion_zone.hpp"
#include "nav2_msgs/srv/add_shapes.hpp"
#include "nav2_msgs/srv/clear_costmap_around_pose.hpp"
#include "nav2_msgs/srv/clear_costmap_around_robot.hpp"
#include "nav2_msgs/srv/clear_costmap_except_region.hpp"
#include "nav2_msgs/srv/clear_entire_costmap.hpp"
#include "nav2_msgs/srv/dynamic_edges.hpp"
#include "nav2_msgs/srv/get_costmap.hpp"
#include "nav2_msgs/srv/get_shapes.hpp"
#include "nav2_msgs/srv/get_costs.hpp"
#include "nav2_msgs/srv/is_path_valid.hpp"
#include "nav2_msgs/srv/load_map.hpp"
#include "nav2_msgs/srv/manage_lifecycle_nodes.hpp"
#include "nav2_msgs/srv/reload_dock_database.hpp"
#include "nav2_msgs/srv/remove_exclusion_zone.hpp"
#include "nav2_msgs/srv/remove_shapes.hpp"
#include "nav2_msgs/srv/save_map.hpp"
#include "nav2_msgs/srv/set_initial_pose.hpp"
#include "nav2_msgs/srv/set_route_graph.hpp"
#include "nav2_msgs/srv/toggle.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "nav_msgs/srv/get_map.hpp"
#include "slam_toolbox/srv/add_submap.hpp"
#include "slam_toolbox/srv/clear.hpp"
#include "slam_toolbox/srv/clear_queue.hpp"
#include "slam_toolbox/srv/deserialize_pose_graph.hpp"
#include "slam_toolbox/srv/loop_closure.hpp"
#include "slam_toolbox/srv/merge_maps.hpp"
#include "slam_toolbox/srv/pause.hpp"
#include "slam_toolbox/srv/reset.hpp"
#include "slam_toolbox/srv/save_map.hpp"
#include "slam_toolbox/srv/serialize_pose_graph.hpp"
#include "slam_toolbox/srv/toggle_interactive.hpp"
#include "geometry_msgs/msg/point32.hpp"
#include "unique_identifier_msgs/msg/uuid.hpp"
#include "std_srvs/srv/empty.hpp"
#include "std_srvs/srv/set_bool.hpp"
#include "std_srvs/srv/trigger.hpp"

namespace ros2_bridge
{
namespace
{

// ----------------------------------------------------------- pretty printing

// The pose half of `nav2_msgs/srv/SetInitialPose`, which is a
// `PoseWithCovarianceStamped` - one line is enough for the position and the
// frame, and the covariance is 36 numbers nobody reads in a log.
std::string describePose(const geometry_msgs::msg::PoseWithCovarianceStamped & stamped)
{
  std::ostringstream out;
  out << "(" << stamped.pose.pose.position.x << ", " << stamped.pose.pose.position.y
      << ", " << stamped.pose.pose.position.z << ") in \"" << stamped.header.frame_id
      << "\" quaternion (" << stamped.pose.pose.orientation.x << ", "
      << stamped.pose.pose.orientation.y << ", " << stamped.pose.pose.orientation.z << ", "
      << stamped.pose.pose.orientation.w << ")\n";
  return out.str();
}

// A grid answer at a glance: `GetMap` (`slam_toolbox/dynamic_map`) and
// `nav2_msgs/srv/LoadMap` both hand back a `nav_msgs/OccupancyGrid`, and the
// cell convention is the one `type_registry.cpp` prints for a map topic
// (-1 unknown, 0 free, 100 occupied).
std::string describeGrid(const nav_msgs::msg::OccupancyGrid & grid)
{
  std::size_t occupied = 0;
  for (const int8_t cell : grid.data) {
    if (cell >= 100) {
      ++occupied;
    }
  }
  std::ostringstream out;
  out << "map:\n"
      << "  frame_id: \"" << grid.header.frame_id << "\"\n"
      << "  resolution: " << grid.info.resolution << "\n"
      << "  size: " << grid.info.width << " x " << grid.info.height << "\n"
      << "  origin: (" << grid.info.origin.position.x << ", "
      << grid.info.origin.position.y << ")\n"
      << "  data: " << grid.data.size() << " cell(s), " << occupied
      << " occupied\n";
  return out.str();
}

// `nav2_msgs/Costmap` is the other map shape in this table - the costmap
// servers' own message, with a layer name and uint8 costs.
std::string describeCostmap(const nav2_msgs::msg::Costmap & costmap)
{
  std::ostringstream out;
  out << "costmap:\n"
      << "  frame_id: \"" << costmap.header.frame_id << "\"\n"
      << "  layer: \"" << costmap.metadata.layer << "\"\n"
      << "  resolution: " << costmap.metadata.resolution << "\n"
      << "  size: " << costmap.metadata.size_x << " x " << costmap.metadata.size_y << "\n"
      << "  origin: (" << costmap.metadata.origin.position.x << ", "
      << costmap.metadata.origin.position.y << ")\n"
      << "  data: " << costmap.data.size() << " cell(s)\n";
  return out.str();
}

// The `plugins` list of the clear-costmap family is "which layers to clear";
// leaving it empty is the useful default (every layer), which is worth saying
// out loud instead of printing an empty list.
void printPlugins(std::ostringstream & out, const std::vector<std::string> & plugins)
{
  if (plugins.empty()) {
    out << "plugins: (all layers)\n";
    return;
  }
  out << "plugins: [" << plugins.size() << "]\n";
  for (const auto & plugin : plugins) {
    out << "  - " << plugin << "\n";
  }
}

// `geometry_msgs/PoseStamped`, the target of `ClearCostmapAroundPose`.
std::string describePoseStamped(const geometry_msgs::msg::PoseStamped & stamped)
{
  std::ostringstream out;
  out << "(" << stamped.pose.position.x << ", " << stamped.pose.position.y << ", "
      << stamped.pose.position.z << ") in \"" << stamped.header.frame_id << "\"\n";
  return out.str();
}

// `unique_identifier_msgs/UUID` is sixteen bytes and every other ROS2 tool
// prints it as hex, so the object servers' ids are readable here too.
std::string describeUuid(const unique_identifier_msgs::msg::UUID & uuid)
{
  static const char * digits = "0123456789abcdef";
  std::string text;
  text.reserve(uuid.uuid.size() * 2);
  for (const uint8_t byte : uuid.uuid) {
    text.push_back(digits[byte >> 4]);
    text.push_back(digits[byte & 0x0F]);
  }
  return text;
}

// The vertex list of a zone or a polygon. The count is what tells a caller
// whether the shape it sent has any geometry in it at all.
void printPointCount(
  std::ostringstream & out, const char * label,
  const std::vector<geometry_msgs::msg::Point32> & points)
{
  out << label << ": " << points.size() << " point(s)\n";
}

// `nav2_msgs/msg/ExclusionZoneDescription`, the one field of
// `AddExclusionZone`: a circle when `radius` is set, a polygon when `points`
// is. `points` is a nested message list, so only its count is printed (see
// the note on `GetCosts.poses` for why it has no command line spelling).
std::string describeExclusionZone(
  const nav2_msgs::msg::ExclusionZoneDescription & zone)
{
  std::ostringstream out;
  out << "zone:\n"
      << "  zone_name: \"" << zone.zone_name << "\"\n"
      << "  type: \"" << zone.type << "\"\n"
      << "  frame_id: \"" << zone.frame_id << "\"\n";
  printPointCount(out, "  points", zone.points);
  out << "  radius: " << zone.radius << "\n"
      << "  min_height: " << zone.min_height << "\n"
      << "  max_height: " << zone.max_height << "\n"
      << "  enabled: " << (zone.enabled ? "true" : "false") << "\n"
      << "  visualize: " << (zone.visualize ? "true" : "false") << "\n"
      << "  frame_hold_timeout: " << zone.frame_hold_timeout << "\n";
  return out.str();
}

// `nav2_msgs/msg/CircleObject` / `PolygonObject`: the tracked obstacles of the
// object servers (`vector_object_server`), shared by `AddShapes` and the
// `GetShapes` answer.
void printCircle(
  std::ostringstream & out, const nav2_msgs::msg::CircleObject & circle)
{
  out << "  - circle " << describeUuid(circle.uuid) << ": center ("
      << circle.center.x << ", " << circle.center.y << ", " << circle.center.z
      << ") radius " << circle.radius << " value "
      << static_cast<int>(circle.value) << " fill "
      << (circle.fill ? "true" : "false") << " in \"" << circle.header.frame_id
      << "\"\n";
}

void printPolygon(
  std::ostringstream & out, const nav2_msgs::msg::PolygonObject & polygon)
{
  out << "  - polygon " << describeUuid(polygon.uuid) << ": "
      << polygon.points.size() << " point(s), "
      << (polygon.closed ? "closed" : "open") << ", value "
      << static_cast<int>(polygon.value) << " in \"" << polygon.header.frame_id
      << "\"\n";
}

// The shape pair of `AddShapes` / `GetShapes`, which is the same two lists in
// both directions.
void printShapes(
  std::ostringstream & out, const std::vector<nav2_msgs::msg::CircleObject> & circles,
  const std::vector<nav2_msgs::msg::PolygonObject> & polygons)
{
  out << "circles: [" << circles.size() << "]\n";
  for (const auto & circle : circles) {
    printCircle(out, circle);
  }
  out << "polygons: [" << polygons.size() << "]\n";
  for (const auto & polygon : polygons) {
    printPolygon(out, polygon);
  }
}

// `geometry_msgs/Pose2D` is `(x, y, theta)` and nothing else. It is the
// `initial_pose` of `slam_toolbox/srv/DeserializePoseGraph`, the map framed
// position a saved pose graph is resumed at.
std::string describePose2D(const geometry_msgs::msg::Pose2D & pose)
{
  std::ostringstream out;
  out << "(" << pose.x << ", " << pose.y << ", " << pose.theta << ")\n";
  return out.str();
}

template<class Message>
std::string describe(const Message & message)
{
  std::ostringstream out;
  if constexpr (
    std::is_same_v<Message, std_srvs::srv::Trigger::Response> ||
    std::is_same_v<Message, std_srvs::srv::SetBool::Response>)
  {
    out << "success: " << (message.success ? "true" : "false") << "\n"
        << "message: \"" << message.message << "\"\n";
  } else if constexpr (std::is_same_v<Message, lifecycle_msgs::srv::GetState::Response>) {
    out << "current_state:\n"
        << "  id: " << static_cast<int>(message.current_state.id) << "\n"
        << "  label: \"" << message.current_state.label << "\"\n";
  } else if constexpr (
    std::is_same_v<Message, lifecycle_msgs::srv::ChangeState::Response> ||
    std::is_same_v<Message, nav2_msgs::srv::ManageLifecycleNodes::Response>)
  {
    out << "success: " << (message.success ? "true" : "false") << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::Toggle::Request>) {
    out << "enable: " << (message.enable ? "true" : "false") << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::Toggle::Response>) {
    out << "success: " << (message.success ? "true" : "false") << "\n"
        << "message: \"" << message.message << "\"\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::SetInitialPose::Request>) {
    out << "pose: " << describePose(message.pose);
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::SetInitialPose::Response>) {
    out << "(empty)\n";
  } else if constexpr (
    std::is_same_v<Message, lifecycle_msgs::srv::GetAvailableTransitions::Response>)
  {
    out << "available_transitions: [" << message.available_transitions.size() << "]\n";
    for (const auto & entry : message.available_transitions) {
      out << "  - " << static_cast<int>(entry.transition.id) << " '" << entry.transition.label
          << "': " << static_cast<int>(entry.start_state.id) << " '"
          << entry.start_state.label << "' -> " << static_cast<int>(entry.goal_state.id)
          << " '" << entry.goal_state.label << "'\n";
    }
  } else if constexpr (
    std::is_same_v<Message, std_srvs::srv::Empty::Request> ||
    std::is_same_v<Message, std_srvs::srv::Empty::Response> ||
    std::is_same_v<Message, std_srvs::srv::Trigger::Request> ||
    std::is_same_v<Message, lifecycle_msgs::srv::GetState::Request> ||
    std::is_same_v<Message, lifecycle_msgs::srv::GetAvailableTransitions::Request> ||
    // The slam_toolbox half of the node's service surface that carries no
    // field at all: five requests that want nothing from the caller (Clear,
    // LoopClosure, MergeMaps, ToggleInteractive, Pause) and the six answers
    // that have nothing to say back (the same five plus AddSubmap).
    // `nav_msgs/srv/GetMap`'s request is empty too.
    std::is_same_v<Message, slam_toolbox::srv::Clear::Request> ||
    std::is_same_v<Message, slam_toolbox::srv::Clear::Response> ||
    std::is_same_v<Message, slam_toolbox::srv::LoopClosure::Request> ||
    std::is_same_v<Message, slam_toolbox::srv::LoopClosure::Response> ||
    std::is_same_v<Message, slam_toolbox::srv::MergeMaps::Request> ||
    std::is_same_v<Message, slam_toolbox::srv::MergeMaps::Response> ||
    std::is_same_v<Message, slam_toolbox::srv::ToggleInteractive::Request> ||
    std::is_same_v<Message, slam_toolbox::srv::ToggleInteractive::Response> ||
    std::is_same_v<Message, slam_toolbox::srv::Pause::Request> ||
    std::is_same_v<Message, slam_toolbox::srv::ClearQueue::Request> ||
    std::is_same_v<Message, slam_toolbox::srv::AddSubmap::Response> ||
    std::is_same_v<Message, nav_msgs::srv::GetMap::Request> ||
    std::is_same_v<Message, nav2_msgs::srv::GetShapes::Request>)
  {
    out << "(empty)\n";
  } else if constexpr (std::is_same_v<Message, std_srvs::srv::SetBool::Request>) {
    out << "data: " << (message.data ? "true" : "false") << "\n";
  } else if constexpr (std::is_same_v<Message, lifecycle_msgs::srv::ChangeState::Request>) {
    out << "transition:\n"
        << "  id: " << static_cast<int>(message.transition.id) << "\n"
        << "  label: \"" << message.transition.label << "\"\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::ManageLifecycleNodes::Request>) {
    out << "command: " << static_cast<int>(message.command) << "\n";
  } else if constexpr (
    std::is_same_v<Message, slam_toolbox::srv::SerializePoseGraph::Request> ||
    std::is_same_v<Message, slam_toolbox::srv::AddSubmap::Request>)
  {
    out << "filename: \"" << message.filename << "\"\n";
  } else if constexpr (
    std::is_same_v<Message, slam_toolbox::srv::SerializePoseGraph::Response>)
  {
    // 0 is RESULT_SUCCESS and 255 RESULT_FAILED_TO_WRITE_FILE, which is the
    // whole of slam_toolbox/srv/SerializePoseGraph.srv.
    out << "result: " << static_cast<int>(message.result);
    if (message.result ==
      slam_toolbox::srv::SerializePoseGraph::Response::RESULT_SUCCESS)
    {
      out << " (success)\n";
    } else if (message.result ==
      slam_toolbox::srv::SerializePoseGraph::Response::RESULT_FAILED_TO_WRITE_FILE)
    {
      out << " (failed to write file)\n";
    } else {
      out << " (unknown result code)\n";
    }
  } else if constexpr (
    std::is_same_v<Message, slam_toolbox::srv::DeserializePoseGraph::Request>)
  {
    out << "filename: \"" << message.filename << "\"\n"
        << "match_type: " << static_cast<int>(message.match_type) << " (";
    switch (message.match_type) {
      case slam_toolbox::srv::DeserializePoseGraph::Request::START_AT_FIRST_NODE:
        out << "start at first node";
        break;
      case slam_toolbox::srv::DeserializePoseGraph::Request::START_AT_GIVEN_POSE:
        out << "start at given pose";
        break;
      case slam_toolbox::srv::DeserializePoseGraph::Request::LOCALIZE_AT_POSE:
        out << "localize at pose";
        break;
      default:
        out << "unset";
        break;
    }
    out << ")\ninitial_pose: " << describePose2D(message.initial_pose);
  } else if constexpr (
    std::is_same_v<Message, slam_toolbox::srv::DeserializePoseGraph::Response>)
  {
    out << "(empty)\n";
  } else if constexpr (std::is_same_v<Message, slam_toolbox::srv::Reset::Request>) {
    out << "pause_new_measurements: "
        << (message.pause_new_measurements ? "true" : "false") << "\n";
  } else if constexpr (std::is_same_v<Message, slam_toolbox::srv::Reset::Response>) {
    // Reset answers with a single `result`, and RESULT_SUCCESS is the only
    // code the upstream node ever sets - the field is there so the type can
    // grow, not because there is a choice today.
    out << "result: " << static_cast<int>(message.result);
    if (message.result == slam_toolbox::srv::Reset::Response::RESULT_SUCCESS) {
      out << " (success)\n";
    } else {
      out << " (unknown result code)\n";
    }
  } else if constexpr (std::is_same_v<Message, slam_toolbox::srv::SaveMap::Request>) {
    // `std_msgs/String name`: the file stem the map is written under, with
    // the `.pgm` / `.yaml` pair appended by map_saver itself.
    out << "name: \"" << message.name.data << "\"\n";
  } else if constexpr (std::is_same_v<Message, slam_toolbox::srv::SaveMap::Response>) {
    out << "result: " << static_cast<int>(message.result);
    if (message.result == slam_toolbox::srv::SaveMap::Response::RESULT_SUCCESS) {
      out << " (success)\n";
    } else if (message.result ==
      slam_toolbox::srv::SaveMap::Response::RESULT_NO_MAP_RECEIEVD)
    {
      // The misspelling is upstream's, and it is the only spelling the
      // generated header has.
      out << " (no map received)\n";
    } else if (message.result ==
      slam_toolbox::srv::SaveMap::Response::RESULT_UNDEFINED_FAILURE)
    {
      out << " (undefined failure)\n";
    } else {
      out << " (unknown result code)\n";
    }
  } else if constexpr (
    std::is_same_v<Message, slam_toolbox::srv::ClearQueue::Response> ||
    std::is_same_v<Message, slam_toolbox::srv::Pause::Response>)
  {
    out << "status: " << (message.status ? "true" : "false") << "\n";
  } else if constexpr (std::is_same_v<Message, nav_msgs::srv::GetMap::Response>) {
    // The occupancy grid the node is holding right now, summarised: the
    // cells themselves are thousands of numbers nobody reads in a log.
    out << describeGrid(message.map);
  } else if constexpr (
    // --------------------------------------------------------- nav2's own
    // The costmap servers' clear family, which is what an operator reaches
    // for when the robot is stuck on a phantom obstacle:
    // nav2_costmap_2d/src/clear_costmap_service.cpp:171-244 creates
    // `clear_entirely_*` / `clear_around_*` / `clear_except_*` /
    // `clear_around_pose_*` on every costmap.
    std::is_same_v<Message, nav2_msgs::srv::ClearEntireCostmap::Request>)
  {
    printPlugins(out, message.plugins);
  } else if constexpr (
    std::is_same_v<Message, nav2_msgs::srv::ClearCostmapAroundRobot::Request> ||
    std::is_same_v<Message, nav2_msgs::srv::ClearCostmapExceptRegion::Request>)
  {
    out << "reset_distance: " << message.reset_distance << "\n";
    printPlugins(out, message.plugins);
  } else if constexpr (
    std::is_same_v<Message, nav2_msgs::srv::ClearCostmapAroundPose::Request>)
  {
    out << "pose: " << describePoseStamped(message.pose)
        << "reset_distance: " << message.reset_distance << "\n";
    printPlugins(out, message.plugins);
  } else if constexpr (
    std::is_same_v<Message, nav2_msgs::srv::ClearEntireCostmap::Response> ||
    std::is_same_v<Message, nav2_msgs::srv::ClearCostmapAroundRobot::Response> ||
    std::is_same_v<Message, nav2_msgs::srv::ClearCostmapExceptRegion::Response> ||
    std::is_same_v<Message, nav2_msgs::srv::ClearCostmapAroundPose::Response>)
  {
    out << "success: " << (message.success ? "true" : "false") << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::GetCostmap::Request>) {
    // The specs a caller *asks* for. Left alone - which is what every caller
    // does, rviz2 included - the service answers with the costmap as it is.
    out << "specs: {layer: \"" << message.specs.layer << "\", resolution: "
        << message.specs.resolution << ", size: " << message.specs.size_x << " x "
        << message.specs.size_y << "}\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::GetCostmap::Response>) {
    out << describeCostmap(message.map);
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::GetCosts::Request>) {
    out << "use_footprint: " << (message.use_footprint ? "true" : "false") << "\n"
        << "poses: [" << message.poses.size() << "]\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::GetCosts::Response>) {
    // Costs are 0..254 with 255 meaning "lethal" in this message; print the
    // first few and the count rather than a list nobody can read.
    const std::size_t shown = std::min<std::size_t>(message.costs.size(), 8);
    out << "costs: [" << message.costs.size() << "]";
    for (std::size_t i = 0; i < shown; ++i) {
      out << (i == 0 ? " " : ", ") << message.costs[i];
    }
    if (message.costs.size() > shown) {
      out << ", ...";
    }
    out << "\nsuccess: " << (message.success ? "true" : "false") << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::IsPathValid::Request>) {
    out << "path: [" << message.path.poses.size() << " waypoints]\n"
        << "max_cost: " << static_cast<int>(message.max_cost) << "\n"
        << "consider_unknown_as_obstacle: "
        << (message.consider_unknown_as_obstacle ? "true" : "false") << "\n"
        << "layer_name: \"" << message.layer_name << "\"\n"
        << "footprint: \"" << message.footprint << "\"\n"
        << "stop_at_first_collision: "
        << (message.stop_at_first_collision ? "true" : "false") << "\n"
        << "max_lookahead_distance: " << message.max_lookahead_distance << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::IsPathValid::Response>) {
    out << "success: " << (message.success ? "true" : "false") << "\n"
        << "is_valid: " << (message.is_valid ? "true" : "false") << "\n"
        << "invalid_pose_indices: [" << message.invalid_pose_indices.size() << "]\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::LoadMap::Request>) {
    out << "map_url: \"" << message.map_url << "\"\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::LoadMap::Response>) {
    // The map comes back only when the result says so, which is why the grid
    // is printed *inside* the success case.
    out << "result: " << static_cast<int>(message.result);
    if (message.result == nav2_msgs::srv::LoadMap::Response::RESULT_SUCCESS) {
      out << " (success)\n" << describeGrid(message.map);
    } else if (
      message.result == nav2_msgs::srv::LoadMap::Response::RESULT_MAP_DOES_NOT_EXIST)
    {
      out << " (map does not exist)\n";
    } else if (
      message.result == nav2_msgs::srv::LoadMap::Response::RESULT_INVALID_MAP_DATA)
    {
      out << " (invalid map data)\n";
    } else if (
      message.result == nav2_msgs::srv::LoadMap::Response::RESULT_INVALID_MAP_METADATA)
    {
      out << " (invalid map metadata)\n";
    } else if (
      message.result == nav2_msgs::srv::LoadMap::Response::RESULT_UNDEFINED_FAILURE)
    {
      out << " (undefined failure)\n";
    } else {
      out << " (unknown result code)\n";
    }
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::SaveMap::Request>) {
    // `map_saver`'s request: what to save, where, and in which format.
    out << "map_topic: \"" << message.map_topic << "\"\n"
        << "map_url: \"" << message.map_url << "\"\n"
        << "image_format: \"" << message.image_format << "\"\n"
        << "map_mode: \"" << message.map_mode << "\"\n"
        << "free_thresh: " << message.free_thresh << "\n"
        << "occupied_thresh: " << message.occupied_thresh << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::SaveMap::Response>) {
    out << "result: " << (message.result ? "true" : "false") << "\n";
  // ------------------------------------------------------ collision monitor
  // `collision_monitor`'s zone pair, which is how a stop polygon is added and
  // removed at run time (nav2_collision_monitor/src/collision_monitor_node.cpp
  // creates both). The answer says whether the zone was accepted, not just
  // that the call arrived - `success: false` plus a `message` is what a
  // malformed zone looks like.
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::AddExclusionZone::Request>) {
    out << describeExclusionZone(message.zone);
  } else if constexpr (
    std::is_same_v<Message, nav2_msgs::srv::AddExclusionZone::Response> ||
    std::is_same_v<Message, nav2_msgs::srv::RemoveExclusionZone::Response>)
  {
    out << "success: " << (message.success ? "true" : "false") << "\n"
        << "message: \"" << message.message << "\"\n";
  } else if constexpr (
    std::is_same_v<Message, nav2_msgs::srv::RemoveExclusionZone::Request>)
  {
    out << "zone_name: \"" << message.zone_name << "\"\n"
        << "remove_all: " << (message.remove_all ? "true" : "false") << "\n";
  // --------------------------------------------------------- object servers
  // `vector_object_server`'s shape trio, whose two payloads are lists of
  // messages - the widest thing this table carries in a service.
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::AddShapes::Request>) {
    printShapes(out, message.circles, message.polygons);
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::GetShapes::Response>) {
    printShapes(out, message.circles, message.polygons);
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::RemoveShapes::Request>) {
    out << "all_objects: " << (message.all_objects ? "true" : "false") << "\n"
        << "uuids: [" << message.uuids.size() << "]\n";
    for (const auto & uuid : message.uuids) {
      out << "  - " << describeUuid(uuid) << "\n";
    }
  } else if constexpr (
    std::is_same_v<Message, nav2_msgs::srv::AddShapes::Response> ||
    std::is_same_v<Message, nav2_msgs::srv::RemoveShapes::Response> ||
    std::is_same_v<Message, nav2_msgs::srv::DynamicEdges::Response> ||
    std::is_same_v<Message, nav2_msgs::srv::SetRouteGraph::Response> ||
    std::is_same_v<Message, nav2_msgs::srv::ReloadDockDatabase::Response>)
  {
    out << "success: " << (message.success ? "true" : "false") << "\n";
  // ---------------------------------------------------------- route server
  // `route_server`'s graph editing: the two edge lists are `uint16` ids, and
  // `adjust_edges` is a nested `EdgeCost` list (count only, as above).
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::DynamicEdges::Request>) {
    out << "closed_edges: [" << message.closed_edges.size() << "]";
    for (const uint16_t edge : message.closed_edges) {
      out << " " << edge;
    }
    out << "\nopened_edges: [" << message.opened_edges.size() << "]";
    for (const uint16_t edge : message.opened_edges) {
      out << " " << edge;
    }
    out << "\nadjust_edges: [" << message.adjust_edges.size() << "]\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::srv::SetRouteGraph::Request>) {
    out << "graph_filepath: \"" << message.graph_filepath << "\"\n";
  // -------------------------------------------------------- docking server
  } else if constexpr (
    std::is_same_v<Message, nav2_msgs::srv::ReloadDockDatabase::Request>)
  {
    out << "filepath: \"" << message.filepath << "\"\n";
  } else {
    out << "(no pretty printer registered for this service type)\n";
  }
  return out.str();
}

// ------------------------------------------------------------- entry point

template<class Message>
std::string describeMessage(const Bytes & shim)
{
  Message message;
  if (!ros2_shim::deserialize(shim.data(), shim.size(), message)) {
    return std::string("(undecodable payload)");
  }
  return describe(message);
}

// ---------------------------------------------------------------- building

#define ASSIGN(EXPR) \
  if (path == #EXPR) { \
    handled = true; \
    if (!setScalar(request.EXPR, value)) { \
      error = "invalid value for " #EXPR ": '" + value + "'"; \
      return false; \
    } \
  }

// The list-valued half, shared with type_registry.cpp: `plugins=a,b` is the
// `string[] plugins` of the clear-costmap family.
#define ASSIGN_SEQ(EXPR) \
  if (path == #EXPR) { \
    handled = true; \
    if (!setSequence(request.EXPR, value)) { \
      error = "invalid list for " #EXPR ": '" + value + "'"; \
      return false; \
    } \
  }

// The request half of a service, from the `field=value` assignments of the
// command line. A request with no fields (Trigger, Empty, GetState) accepts no
// assignment at all, and a typo is an error rather than a silently ignored
// argument - that is the whole point of taking them on the command line.
template<class Request>
bool applyRequestFields(
  Request & request, const std::vector<FieldAssign> & fields, std::string & error)
{
  for (const auto & field : fields) {
    const std::string & path = field.path;
    const std::string & value = field.value;
    bool handled = false;
    if constexpr (std::is_same_v<Request, std_srvs::srv::SetBool::Request>) {
      ASSIGN(data)
    } else if constexpr (std::is_same_v<Request, lifecycle_msgs::srv::ChangeState::Request>) {
      ASSIGN(transition.id) else ASSIGN(transition.label)
    } else if constexpr (std::is_same_v<Request, nav2_msgs::srv::ManageLifecycleNodes::Request>) {
      ASSIGN(command)
    } else if constexpr (std::is_same_v<Request, nav2_msgs::srv::Toggle::Request>) {
      ASSIGN(enable)
    } else if constexpr (std::is_same_v<Request, nav2_msgs::srv::SetInitialPose::Request>) {
      ASSIGN(pose.header.frame_id)
      ASSIGN(pose.header.stamp.sec)
      ASSIGN(pose.header.stamp.nanosec)
      ASSIGN(pose.pose.pose.position.x)
      ASSIGN(pose.pose.pose.position.y)
      ASSIGN(pose.pose.pose.position.z)
      ASSIGN(pose.pose.pose.orientation.x)
      ASSIGN(pose.pose.pose.orientation.y)
      ASSIGN(pose.pose.pose.orientation.z)
      ASSIGN(pose.pose.pose.orientation.w)
    } else if constexpr (
      std::is_same_v<Request, slam_toolbox::srv::SerializePoseGraph::Request>)
    {
      ASSIGN(filename)
    } else if constexpr (std::is_same_v<Request, slam_toolbox::srv::AddSubmap::Request>) {
      ASSIGN(filename)
    } else if constexpr (std::is_same_v<Request, slam_toolbox::srv::SaveMap::Request>) {
      // `std_msgs/String name`; the one field inside it is `data`.
      ASSIGN(name.data)
    } else if constexpr (std::is_same_v<Request, slam_toolbox::srv::Reset::Request>) {
      ASSIGN(pause_new_measurements)
    } else if constexpr (
      std::is_same_v<Request, slam_toolbox::srv::DeserializePoseGraph::Request>)
    {
      ASSIGN(filename)
      ASSIGN(match_type)
      ASSIGN(initial_pose.x)
      ASSIGN(initial_pose.y)
      ASSIGN(initial_pose.theta)
    } else if constexpr (std::is_same_v<Request, nav2_msgs::srv::ClearEntireCostmap::Request>) {
      ASSIGN_SEQ(plugins)
    } else if constexpr (
      std::is_same_v<Request, nav2_msgs::srv::ClearCostmapAroundRobot::Request> ||
      std::is_same_v<Request, nav2_msgs::srv::ClearCostmapExceptRegion::Request>)
    {
      ASSIGN(reset_distance)
      ASSIGN_SEQ(plugins)
    } else if constexpr (
      std::is_same_v<Request, nav2_msgs::srv::ClearCostmapAroundPose::Request>)
    {
      ASSIGN(pose.header.frame_id)
      ASSIGN(pose.pose.position.x)
      ASSIGN(pose.pose.position.y)
      ASSIGN(pose.pose.position.z)
      ASSIGN(pose.pose.orientation.x)
      ASSIGN(pose.pose.orientation.y)
      ASSIGN(pose.pose.orientation.z)
      ASSIGN(pose.pose.orientation.w)
      ASSIGN(reset_distance)
      ASSIGN_SEQ(plugins)
    } else if constexpr (std::is_same_v<Request, nav2_msgs::srv::GetCosts::Request>) {
      // `poses` is a `geometry_msgs/PoseStamped[]`, and there is no useful
      // command-line spelling for a list of nested messages; the cost at the
      // robot's own pose is what the empty list means.
      ASSIGN(use_footprint)
    } else if constexpr (std::is_same_v<Request, nav2_msgs::srv::IsPathValid::Request>) {
      ASSIGN(max_cost)
      ASSIGN(consider_unknown_as_obstacle)
      ASSIGN(layer_name)
      ASSIGN(footprint)
      ASSIGN(stop_at_first_collision)
      ASSIGN(max_lookahead_distance)
    } else if constexpr (std::is_same_v<Request, nav2_msgs::srv::LoadMap::Request>) {
      ASSIGN(map_url)
    } else if constexpr (std::is_same_v<Request, nav2_msgs::srv::SaveMap::Request>) {
      ASSIGN(map_topic)
      ASSIGN(map_url)
      ASSIGN(image_format)
      ASSIGN(map_mode)
      ASSIGN(free_thresh)
      ASSIGN(occupied_thresh)
    } else if constexpr (std::is_same_v<Request, nav2_msgs::srv::AddExclusionZone::Request>) {
      // `zone.points` is a list of `Point32`, which has no command line
      // spelling; a circular zone (`radius`, no points) is what a caller can
      // express, and it is the shape nav2's own parameters describe.
      ASSIGN(zone.zone_name)
      ASSIGN(zone.type)
      ASSIGN(zone.frame_id)
      ASSIGN(zone.radius)
      ASSIGN(zone.min_height)
      ASSIGN(zone.max_height)
      ASSIGN(zone.enabled)
      ASSIGN(zone.visualize)
      ASSIGN(zone.frame_hold_timeout)
    } else if constexpr (
      std::is_same_v<Request, nav2_msgs::srv::RemoveExclusionZone::Request>)
    {
      ASSIGN(zone_name)
      ASSIGN(remove_all)
    } else if constexpr (std::is_same_v<Request, nav2_msgs::srv::AddShapes::Request>) {
      // Both fields are lists of messages, so the useful call is the empty
      // pair - "add nothing, tell me if that is accepted".
      (void)request;
    } else if constexpr (std::is_same_v<Request, nav2_msgs::srv::RemoveShapes::Request>) {
      // `uuids` is the nested list again; `all_objects` is the useful half.
      ASSIGN(all_objects)
    } else if constexpr (std::is_same_v<Request, nav2_msgs::srv::DynamicEdges::Request>) {
      // The `uint16` edge lists are settable; `adjust_edges` is not.
      ASSIGN_SEQ(closed_edges)
      ASSIGN_SEQ(opened_edges)
    } else if constexpr (std::is_same_v<Request, nav2_msgs::srv::SetRouteGraph::Request>) {
      ASSIGN(graph_filepath)
    } else if constexpr (
      std::is_same_v<Request, nav2_msgs::srv::ReloadDockDatabase::Request>)
    {
      ASSIGN(filepath)
    } else {
      (void)request;
      (void)value;
    }
    if (!handled) {
      error = "service type '" + std::string(Request::ros2_message_type) +
        "' has no settable field '" + path + "'";
      return false;
    }
  }
  return true;
}

#undef ASSIGN
#undef ASSIGN_SEQ

template<class ServiceT>
ServiceCodec makeService(const char * type)
{
  ServiceCodec codec;
  codec.type = type;
  codec.request_to_cdr = &shimToCdr<typename ServiceT::Request>;
  codec.response_to_shim = &cdrToShim<typename ServiceT::Response>;
  codec.request_to_shim = &cdrToShim<typename ServiceT::Request>;
  codec.response_to_cdr = &shimToCdr<typename ServiceT::Response>;
  codec.describe_request = &describeMessage<typename ServiceT::Request>;
  codec.describe_response = &describeMessage<typename ServiceT::Response>;
  codec.build_request = [](const std::vector<FieldAssign> & fields, Bytes & shim, std::string & error) {
      typename ServiceT::Request request{};
      if (!applyRequestFields(request, fields, error)) {
        return false;
      }
      shim = ros2_shim::serialize(request);
      return true;
    };
  return codec;
}

const std::map<std::string, ServiceCodec> & registry()
{
  static const std::map<std::string, ServiceCodec> table = {
    {"std_srvs/srv/Empty", makeService<std_srvs::srv::Empty>("std_srvs/srv/Empty")},
    {"std_srvs/srv/Trigger", makeService<std_srvs::srv::Trigger>("std_srvs/srv/Trigger")},
    {"std_srvs/srv/SetBool", makeService<std_srvs::srv::SetBool>("std_srvs/srv/SetBool")},
    {"lifecycle_msgs/srv/GetState",
      makeService<lifecycle_msgs::srv::GetState>("lifecycle_msgs/srv/GetState")},
    {"lifecycle_msgs/srv/ChangeState",
      makeService<lifecycle_msgs::srv::ChangeState>("lifecycle_msgs/srv/ChangeState")},
    {"lifecycle_msgs/srv/GetAvailableTransitions",
      makeService<lifecycle_msgs::srv::GetAvailableTransitions>(
        "lifecycle_msgs/srv/GetAvailableTransitions")},
    {"nav2_msgs/srv/ManageLifecycleNodes",
      makeService<nav2_msgs::srv::ManageLifecycleNodes>(
        "nav2_msgs/srv/ManageLifecycleNodes")},

    // The two nav2 services a *robot* serves rather than a server on this
    // machine, which is the direction the bridge carries services in: the
    // agent advertises them and the daemon mirrors them here, so
    // `ros2 service call /sim/set_initial_pose ...` reaches the robot's own
    // implementation. Anything the robot does not advertise still shows up in
    // `ros2 service list` - it is the *codec* that has to exist here, and an
    // unknown type is reported as one rather than half-decoded.
    {"nav2_msgs/srv/SetInitialPose",
      makeService<nav2_msgs::srv::SetInitialPose>("nav2_msgs/srv/SetInitialPose")},
    {"nav2_msgs/srv/Toggle",
      makeService<nav2_msgs::srv::Toggle>("nav2_msgs/srv/Toggle")},

    // The rest of the nav2 servers' service surface: every costmap answers
    // four clear services - created by
    // nav2_costmap_2d/src/clear_costmap_service.cpp:171-244 - and the planner
    // and the map server add three more that a live stack advertises.
    // Without these the CLI answers `no codec for service type ...` for the
    // one call an operator reaches for most often, clearing a costmap.
    {"nav2_msgs/srv/ClearEntireCostmap",
      makeService<nav2_msgs::srv::ClearEntireCostmap>(
        "nav2_msgs/srv/ClearEntireCostmap")},
    {"nav2_msgs/srv/ClearCostmapAroundRobot",
      makeService<nav2_msgs::srv::ClearCostmapAroundRobot>(
        "nav2_msgs/srv/ClearCostmapAroundRobot")},
    {"nav2_msgs/srv/ClearCostmapExceptRegion",
      makeService<nav2_msgs::srv::ClearCostmapExceptRegion>(
        "nav2_msgs/srv/ClearCostmapExceptRegion")},
    {"nav2_msgs/srv/ClearCostmapAroundPose",
      makeService<nav2_msgs::srv::ClearCostmapAroundPose>(
        "nav2_msgs/srv/ClearCostmapAroundPose")},

    // `GetCostmap` answers with a `nav2_msgs/Costmap`, which is the one
    // response in this table that carries a nested message with a `uint8[]`
    // inside it; `GetCosts` and `IsPathValid` are the planner's other two
    // questions.
    {"nav2_msgs/srv/GetCostmap",
      makeService<nav2_msgs::srv::GetCostmap>("nav2_msgs/srv/GetCostmap")},
    {"nav2_msgs/srv/GetCosts",
      makeService<nav2_msgs::srv::GetCosts>("nav2_msgs/srv/GetCosts")},
    {"nav2_msgs/srv/IsPathValid",
      makeService<nav2_msgs::srv::IsPathValid>("nav2_msgs/srv/IsPathValid")},

    // The map server's pair: `LoadMap` reads a YAML into an OccupancyGrid,
    // `SaveMap` writes the map currently on a topic back out. Both are the
    // service spelling of what `map_server.exe` / `map_saver_cli.exe` do at
    // startup and shutdown.
    {"nav2_msgs/srv/LoadMap",
      makeService<nav2_msgs::srv::LoadMap>("nav2_msgs/srv/LoadMap")},
    {"nav2_msgs/srv/SaveMap",
      makeService<nav2_msgs::srv::SaveMap>("nav2_msgs/srv/SaveMap")},
    // The last eight of the twenty `nav2_msgs` srv: the collision monitor's
    // zone pair, the object servers' shape trio, the route server's two, and
    // the docking server's database reload. All four nodes are this port's own
    // binaries, so every one of them can be called on a live graph.
    {"nav2_msgs/srv/AddExclusionZone",
      makeService<nav2_msgs::srv::AddExclusionZone>("nav2_msgs/srv/AddExclusionZone")},
    {"nav2_msgs/srv/RemoveExclusionZone",
      makeService<nav2_msgs::srv::RemoveExclusionZone>(
        "nav2_msgs/srv/RemoveExclusionZone")},
    {"nav2_msgs/srv/AddShapes",
      makeService<nav2_msgs::srv::AddShapes>("nav2_msgs/srv/AddShapes")},
    {"nav2_msgs/srv/RemoveShapes",
      makeService<nav2_msgs::srv::RemoveShapes>("nav2_msgs/srv/RemoveShapes")},
    {"nav2_msgs/srv/GetShapes",
      makeService<nav2_msgs::srv::GetShapes>("nav2_msgs/srv/GetShapes")},
    {"nav2_msgs/srv/DynamicEdges",
      makeService<nav2_msgs::srv::DynamicEdges>("nav2_msgs/srv/DynamicEdges")},
    {"nav2_msgs/srv/SetRouteGraph",
      makeService<nav2_msgs::srv::SetRouteGraph>("nav2_msgs/srv/SetRouteGraph")},
    {"nav2_msgs/srv/ReloadDockDatabase",
      makeService<nav2_msgs::srv::ReloadDockDatabase>(
        "nav2_msgs/srv/ReloadDockDatabase")},

    // slam_toolbox's pose graph archive. Unlike the two above these are served
    // by a node on *this* machine - the ported one creates both services in
    // setROSInterfaces() (third_party/slam_toolbox/src/slam_toolbox_common.cpp
    // :459 and :463) - so the request leaves through the shared-memory channel
    // and the answer comes back the same way, with no daemon in the middle.
    // The `<name>.posegraph` + `<name>.data` pair written here is what
    // `-p map_file_name:=<name>` reads back at configure time.
    {"slam_toolbox/srv/SerializePoseGraph",
      makeService<slam_toolbox::srv::SerializePoseGraph>(
        "slam_toolbox/srv/SerializePoseGraph")},
    {"slam_toolbox/srv/DeserializePoseGraph",
      makeService<slam_toolbox::srv::DeserializePoseGraph>(
        "slam_toolbox/srv/DeserializePoseGraph")},

    // The rest of that node's service surface, served the same way - the
    // codecs here are what make `ros2 service call slam_toolbox/reset ...`
    // and the eight names beside it reach the ported node over shared
    // memory: third_party/slam_toolbox/src/slam_toolbox_common.cpp:454-467
    // (pause, reset), loop_closure_assistant.cpp:54-71 (clear, manual loop
    // closure, interactive mode), map_saver.cpp:34 (save_map),
    // merge_maps_kinematic.cpp:50-53 (merge, add submap) and
    // slam_toolbox_sync.cpp:73 (clear_queue).
    {"slam_toolbox/srv/AddSubmap",
      makeService<slam_toolbox::srv::AddSubmap>("slam_toolbox/srv/AddSubmap")},
    {"slam_toolbox/srv/Clear",
      makeService<slam_toolbox::srv::Clear>("slam_toolbox/srv/Clear")},
    {"slam_toolbox/srv/ClearQueue",
      makeService<slam_toolbox::srv::ClearQueue>("slam_toolbox/srv/ClearQueue")},
    {"slam_toolbox/srv/LoopClosure",
      makeService<slam_toolbox::srv::LoopClosure>("slam_toolbox/srv/LoopClosure")},
    {"slam_toolbox/srv/MergeMaps",
      makeService<slam_toolbox::srv::MergeMaps>("slam_toolbox/srv/MergeMaps")},
    {"slam_toolbox/srv/Pause",
      makeService<slam_toolbox::srv::Pause>("slam_toolbox/srv/Pause")},
    {"slam_toolbox/srv/Reset",
      makeService<slam_toolbox::srv::Reset>("slam_toolbox/srv/Reset")},
    {"slam_toolbox/srv/SaveMap",
      makeService<slam_toolbox::srv::SaveMap>("slam_toolbox/srv/SaveMap")},
    {"slam_toolbox/srv/ToggleInteractive",
      makeService<slam_toolbox::srv::ToggleInteractive>(
        "slam_toolbox/srv/ToggleInteractive")},

    // `slam_toolbox/dynamic_map` is the one service the node serves under a
    // nav_msgs type (slam_toolbox_common.cpp:451), so `nav_msgs/srv/GetMap`
    // belongs beside slam_toolbox's own rather than with the nav2_msgs pair
    // above: it is the map the node is holding at the moment of the call.
    {"nav_msgs/srv/GetMap",
      makeService<nav_msgs::srv::GetMap>("nav_msgs/srv/GetMap")},
  };
  return table;
}

}  // namespace

std::string canonical_service_type(const std::string & type_name)
{
  if (type_name.empty()) {
    return type_name;
  }
  const std::size_t first = type_name.find('/');
  if (first == std::string::npos) {
    return type_name;
  }
  if (type_name.find('/', first + 1) != std::string::npos) {
    return type_name;
  }
  // `std_srvs/Trigger` and `Trigger` are both spelled out in ROS2 tools; the
  // canonical form carries the kind, like `std_srvs/srv/Trigger`.
  return type_name.substr(0, first) + "/srv/" + type_name.substr(first + 1);
}

std::string service_request_type(const std::string & service_type)
{
  return canonical_service_type(service_type) + "_Request";
}

std::string service_response_type(const std::string & service_type)
{
  return canonical_service_type(service_type) + "_Response";
}

const ServiceCodec * find_service_codec(const std::string & type_name)
{
  const auto & table = registry();
  const auto it = table.find(canonical_service_type(type_name));
  return it == table.end() ? nullptr : &it->second;
}

bool is_supported_service(const std::string & type_name)
{
  return find_service_codec(type_name) != nullptr;
}

std::vector<std::string> supported_services()
{
  std::vector<std::string> names;
  for (const auto & entry : registry()) {
    names.push_back(entry.first);
  }
  return names;
}

}  // namespace ros2_bridge
