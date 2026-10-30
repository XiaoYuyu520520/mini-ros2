#include "ros2_bridge/action_registry.hpp"

#include <map>
#include <sstream>
#include <string>
#include <type_traits>

#include "ros2_bridge/field_parse.hpp"
#include "ros2_bridge/sim_action_messages.hpp"
#include "ros2_bridge/transcode.hpp"
#include "ros2_shim/serialization.hpp"

#include "builtin_interfaces/msg/duration.hpp"
#include "geometry_msgs/msg/point.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "nav2_msgs/action/assisted_teleop.hpp"
#include "nav2_msgs/action/back_up.hpp"
#include "nav2_msgs/action/compute_and_track_route.hpp"
#include "nav2_msgs/action/compute_path_through_poses.hpp"
#include "nav2_msgs/action/compute_path_to_pose.hpp"
#include "nav2_msgs/action/compute_route.hpp"
#include "nav2_msgs/action/dock_robot.hpp"
#include "nav2_msgs/action/drive_on_heading.hpp"
#include "nav2_msgs/action/dummy_behavior.hpp"
#include "nav2_msgs/action/follow_gps_waypoints.hpp"
#include "nav2_msgs/action/follow_object.hpp"
#include "nav2_msgs/action/follow_path.hpp"
#include "nav2_msgs/action/follow_waypoints.hpp"
#include "nav2_msgs/action/navigate_through_poses.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "nav2_msgs/action/smooth_path.hpp"
#include "nav2_msgs/action/spin.hpp"
#include "nav2_msgs/action/undock_robot.hpp"
#include "nav2_msgs/action/wait.hpp"
#include "nav2_msgs/msg/route.hpp"
#include "nav_msgs/msg/goals.hpp"
#include "nav_msgs/msg/path.hpp"
#include "std_msgs/msg/empty.hpp"
#include "std_msgs/msg/string.hpp"

namespace ros2_bridge
{
namespace
{

// ----------------------------------------------------------- pretty printing

// `uint8_t` and `int16_t` stream as characters, so an error code of 0 would
// print as a NUL in the middle of the line someone is staring at. Every narrow
// field goes through here first.
template<class T>
int widen(T value)
{
  return static_cast<int>(value);
}

std::string boolText(bool value)
{
  return value ? "true" : "false";
}

std::string seconds(const builtin_interfaces::msg::Duration & duration)
{
  std::ostringstream out;
  out << duration.sec << "s+" << duration.nanosec << "ns";
  return out.str();
}

std::string point(const geometry_msgs::msg::Point & position)
{
  std::ostringstream out;
  out << "(" << position.x << ", " << position.y << ", " << position.z << ")";
  return out.str();
}

std::string pose(const geometry_msgs::msg::PoseStamped & stamped)
{
  std::ostringstream out;
  out << "(" << stamped.pose.position.x << ", " << stamped.pose.position.y
      << ", " << stamped.pose.position.z << ") in \"" << stamped.header.frame_id
      << "\" yaw-ish quaternion (" << stamped.pose.orientation.x << ", "
      << stamped.pose.orientation.y << ", " << stamped.pose.orientation.z << ", "
      << stamped.pose.orientation.w << ")";
  return out.str();
}

// A `nav_msgs/Path` as it shows up in a goal or a result: the one field a
// caller can read at a glance, since the poses themselves are a sequence.
std::string pathText(const nav_msgs::msg::Path & path)
{
  std::ostringstream out;
  out << path.poses.size() << " pose(s) in \"" << path.header.frame_id << "\"";
  return out.str();
}

// `nav_msgs/Goals` is the same idea for the `*ThroughPoses` pair.
std::string goalsText(const nav_msgs::msg::Goals & goals)
{
  std::ostringstream out;
  out << goals.goals.size() << " goal(s) in \"" << goals.header.frame_id << "\"";
  return out.str();
}

// A route, which is a graph walk rather than a path: the counts and the cost
// are what a caller checks, the nodes and edges themselves are a sequence.
std::string routeText(const nav2_msgs::msg::Route & route)
{
  std::ostringstream out;
  out << route.nodes.size() << " node(s), " << route.edges.size()
      << " edge(s), cost " << route.route_cost;
  return out.str();
}

template<class Message>
std::string describe(const Message & message)
{
  std::ostringstream out;
  if constexpr (std::is_same_v<Message, ros2_bridge_sim::action::Drive::Goal>) {
    out << "distance: " << message.distance << "\n";
  } else if constexpr (std::is_same_v<Message, ros2_bridge_sim::action::Drive::Feedback>) {
    out << "travelled: " << message.travelled << "\n"
        << "remaining: " << message.remaining << "\n";
  } else if constexpr (std::is_same_v<Message, ros2_bridge_sim::action::Drive::Result>) {
    out << "travelled: " << message.travelled << "\n"
        << "success: " << boolText(message.success) << "\n";

  // ------------------------------------------------------------ nav2 actions
  //
  // The five behaviors `behavior_server` serves (`Spin`, `BackUp`,
  // `DriveOnHeading`, `Wait`, `AssistedTeleop`) plus the three the navigator
  // serves on top of them. Between them they are what a robot that runs nav2
  // *on board* is commanded with, which is exactly the case the bridge has to
  // carry: the caller on this machine sends the same goal it would send to a
  // local server, and never sees the wire.
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::Spin::Goal>) {
    out << "target_yaw: " << message.target_yaw << "\n"
        << "time_allowance: " << seconds(message.time_allowance) << "\n"
        << "disable_collision_checks: " << boolText(message.disable_collision_checks) << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::Spin::Feedback>) {
    out << "angular_distance_traveled: " << message.angular_distance_traveled << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::Spin::Result>) {
    out << "total_elapsed_time: " << seconds(message.total_elapsed_time) << "\n"
        << "error_code: " << widen(message.error_code) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";

  } else if constexpr (
    std::is_same_v<Message, nav2_msgs::action::BackUp::Goal> ||
    std::is_same_v<Message, nav2_msgs::action::DriveOnHeading::Goal>)
  {
    out << "target: " << point(message.target) << "\n"
        << "speed: " << message.speed << "\n"
        << "time_allowance: " << seconds(message.time_allowance) << "\n"
        << "disable_collision_checks: " << boolText(message.disable_collision_checks) << "\n";
  } else if constexpr (
    std::is_same_v<Message, nav2_msgs::action::BackUp::Feedback> ||
    std::is_same_v<Message, nav2_msgs::action::DriveOnHeading::Feedback>)
  {
    out << "distance_traveled: " << message.distance_traveled << "\n";
  } else if constexpr (
    std::is_same_v<Message, nav2_msgs::action::BackUp::Result> ||
    std::is_same_v<Message, nav2_msgs::action::DriveOnHeading::Result>)
  {
    out << "total_elapsed_time: " << seconds(message.total_elapsed_time) << "\n"
        << "error_code: " << widen(message.error_code) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";

  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::Wait::Goal>) {
    out << "time: " << seconds(message.time) << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::Wait::Feedback>) {
    out << "time_left: " << seconds(message.time_left) << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::Wait::Result>) {
    out << "total_elapsed_time: " << seconds(message.total_elapsed_time) << "\n"
        << "error_code: " << widen(message.error_code) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";

  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::NavigateToPose::Goal>) {
    out << "pose: " << pose(message.pose) << "\n"
        << "behavior_tree: \"" << message.behavior_tree << "\"\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::NavigateToPose::Feedback>) {
    out << "current_pose: " << pose(message.current_pose) << "\n"
        << "navigation_time: " << seconds(message.navigation_time) << "\n"
        << "estimated_time_remaining: " << seconds(message.estimated_time_remaining) << "\n"
        << "number_of_recoveries: " << widen(message.number_of_recoveries) << "\n"
        << "distance_remaining: " << message.distance_remaining << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::NavigateToPose::Result>) {
    out << "error_code: " << widen(message.error_code) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";

  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::ComputePathToPose::Goal>) {
    out << "goal: " << pose(message.goal) << "\n"
        << "start: " << pose(message.start) << "\n"
        << "viapoints: " << message.viapoints.size() << "\n"
        << "planner_id: \"" << message.planner_id << "\"\n"
        << "use_start: " << boolText(message.use_start) << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::ComputePathToPose::Result>) {
    out << "path: " << message.path.poses.size() << " pose(s) in \""
        << message.path.header.frame_id << "\"\n"
        << "planning_time: " << seconds(message.planning_time) << "\n"
        << "error_code: " << widen(message.error_code) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";

  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::FollowPath::Goal>) {
    out << "path: " << message.path.poses.size() << " pose(s) in \""
        << message.path.header.frame_id << "\"\n"
        << "controller_id: \"" << message.controller_id << "\"\n"
        << "goal_checker_id: \"" << message.goal_checker_id << "\"\n"
        << "progress_checker_id: \"" << message.progress_checker_id << "\"\n"
        << "path_handler_id: \"" << message.path_handler_id << "\"\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::FollowPath::Feedback>) {
    out << "tracking_feedback: distance_to_goal " << message.tracking_feedback.distance_to_goal
        << ", speed " << message.tracking_feedback.speed
        << ", index " << message.tracking_feedback.current_path_index << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::FollowPath::Result>) {
    out << "error_code: " << widen(message.error_code) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";

  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::FollowWaypoints::Goal>) {
    out << "number_of_loops: " << message.number_of_loops << "\n"
        << "goal_index: " << message.goal_index << "\n"
        << "poses: " << message.poses.size() << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::FollowWaypoints::Feedback>) {
    out << "current_waypoint: " << message.current_waypoint << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::FollowWaypoints::Result>) {
    out << "missed_waypoints: " << message.missed_waypoints.size() << "\n"
        << "error_code: " << widen(message.error_code) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";
  // The rest of nav2's action surface: the second navigator entry point, the
  // route server's pair, the dock server, and the behaviors nav2 ships besides
  // the four above. Every row of the codec table has a printer here - a caller
  // that got a byte count back instead of an answer would be reading a corner
  // of the bridge that had no opinion.
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::AssistedTeleop::Goal>) {
    out << "time_allowance: " << seconds(message.time_allowance) << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::AssistedTeleop::Feedback>) {
    out << "current_teleop_duration: " << seconds(message.current_teleop_duration) << "\n";
  } else if constexpr (
    std::is_same_v<Message, nav2_msgs::action::AssistedTeleop::Result> ||
    std::is_same_v<Message, nav2_msgs::action::DummyBehavior::Result>)
  {
    out << "total_elapsed_time: " << seconds(message.total_elapsed_time) << "\n"
        << "error_code: " << widen(message.error_code) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";

  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::DummyBehavior::Goal>) {
    out << "command: \"" << message.command.data << "\"\n";

  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::SmoothPath::Goal>) {
    out << "path: " << pathText(message.path) << "\n"
        << "smoother_id: \"" << message.smoother_id << "\"\n"
        << "max_smoothing_duration: " << seconds(message.max_smoothing_duration) << "\n"
        << "check_for_collisions: " << boolText(message.check_for_collisions) << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::SmoothPath::Result>) {
    out << "path: " << pathText(message.path) << "\n"
        << "smoothing_duration: " << seconds(message.smoothing_duration) << "\n"
        << "was_completed: " << boolText(message.was_completed) << "\n"
        << "error_code: " << widen(message.error_code) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";

  // The route server: one goal message, two actions over it (see the codec
  // table), so the same printer answers for both.
  } else if constexpr (
    std::is_same_v<Message, nav2_msgs::action::ComputeRoute::Goal> ||
    std::is_same_v<Message, nav2_msgs::action::ComputeAndTrackRoute::Goal>)
  {
    out << "start_id: " << widen(message.start_id) << "\n"
        << "start: " << pose(message.start) << "\n"
        << "goal_id: " << widen(message.goal_id) << "\n"
        << "goal: " << pose(message.goal) << "\n"
        << "use_start: " << boolText(message.use_start) << "\n"
        << "use_poses: " << boolText(message.use_poses) << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::ComputeRoute::Result>) {
    out << "planning_time: " << seconds(message.planning_time) << "\n"
        << "path: " << pathText(message.path) << "\n"
        << "route: " << routeText(message.route) << "\n"
        << "error_code: " << widen(message.error_code) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::ComputeAndTrackRoute::Result>) {
    out << "execution_duration: " << seconds(message.execution_duration) << "\n"
        << "error_code: " << widen(message.error_code) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::ComputeAndTrackRoute::Feedback>) {
    out << "last_node_id: " << widen(message.last_node_id) << "\n"
        << "next_node_id: " << widen(message.next_node_id) << "\n"
        << "current_edge_id: " << widen(message.current_edge_id) << "\n"
        << "route: " << routeText(message.route) << "\n"
        << "path: " << pathText(message.path) << "\n"
        << "operations_triggered: " << message.operations_triggered.size() << "\n"
        << "rerouted: " << boolText(message.rerouted) << "\n";

  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::ComputePathThroughPoses::Goal>) {
    out << "goals: " << goalsText(message.goals) << "\n"
        << "start: " << pose(message.start) << "\n"
        << "planner_id: \"" << message.planner_id << "\"\n"
        << "use_start: " << boolText(message.use_start) << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::ComputePathThroughPoses::Result>) {
    out << "path: " << pathText(message.path) << "\n"
        << "last_reached_index: " << widen(message.last_reached_index) << "\n"
        << "planning_time: " << seconds(message.planning_time) << "\n"
        << "error_code: " << widen(message.error_code) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";

  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::NavigateThroughPoses::Goal>) {
    out << "poses: " << goalsText(message.poses) << "\n"
        << "behavior_tree: \"" << message.behavior_tree << "\"\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::NavigateThroughPoses::Result>) {
    out << "error_code: " << widen(message.error_code) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n"
        << "waypoint_statuses: " << message.waypoint_statuses.size() << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::NavigateThroughPoses::Feedback>) {
    out << "current_pose: " << pose(message.current_pose) << "\n"
        << "navigation_time: " << seconds(message.navigation_time) << "\n"
        << "estimated_time_remaining: " << seconds(message.estimated_time_remaining) << "\n"
        << "number_of_recoveries: " << widen(message.number_of_recoveries) << "\n"
        << "distance_remaining: " << message.distance_remaining << "\n"
        << "position_tracking_error: " << message.position_tracking_error << "\n"
        << "heading_tracking_error: " << message.heading_tracking_error << "\n"
        << "number_of_poses_remaining: " << widen(message.number_of_poses_remaining) << "\n"
        << "waypoint_statuses: " << message.waypoint_statuses.size() << "\n";

  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::DockRobot::Goal>) {
    out << "use_dock_id: " << boolText(message.use_dock_id) << "\n"
        << "dock_id: \"" << message.dock_id << "\"\n"
        << "dock_pose: " << pose(message.dock_pose) << "\n"
        << "dock_type: \"" << message.dock_type << "\"\n"
        << "max_staging_time: " << message.max_staging_time << "\n"
        << "navigate_to_staging_pose: " << boolText(message.navigate_to_staging_pose) << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::DockRobot::Result>) {
    out << "success: " << boolText(message.success) << "\n"
        << "error_code: " << widen(message.error_code) << "\n"
        << "num_retries: " << widen(message.num_retries) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::DockRobot::Feedback>) {
    out << "state: " << widen(message.state) << "\n"
        << "docking_time: " << seconds(message.docking_time) << "\n"
        << "num_retries: " << widen(message.num_retries) << "\n";

  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::UndockRobot::Goal>) {
    out << "dock_type: \"" << message.dock_type << "\"\n"
        << "max_undocking_time: " << message.max_undocking_time << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::UndockRobot::Result>) {
    out << "success: " << boolText(message.success) << "\n"
        << "error_code: " << widen(message.error_code) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";

  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::FollowObject::Goal>) {
    out << "pose_topic: \"" << message.pose_topic << "\"\n"
        << "tracked_frame: \"" << message.tracked_frame << "\"\n"
        << "max_duration: " << seconds(message.max_duration) << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::FollowObject::Result>) {
    out << "total_elapsed_time: " << seconds(message.total_elapsed_time) << "\n"
        << "error_code: " << widen(message.error_code) << "\n"
        << "num_retries: " << widen(message.num_retries) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::FollowObject::Feedback>) {
    out << "state: " << widen(message.state) << "\n"
        << "following_time: " << seconds(message.following_time) << "\n"
        << "num_retries: " << widen(message.num_retries) << "\n";

  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::FollowGPSWaypoints::Goal>) {
    out << "number_of_loops: " << message.number_of_loops << "\n"
        << "goal_index: " << message.goal_index << "\n"
        << "gps_poses: " << message.gps_poses.size() << "\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::FollowGPSWaypoints::Result>) {
    out << "missed_waypoints: " << message.missed_waypoints.size() << "\n"
        << "error_code: " << widen(message.error_code) << "\n"
        << "error_msg: \"" << message.error_msg << "\"\n";
  } else if constexpr (std::is_same_v<Message, nav2_msgs::action::FollowGPSWaypoints::Feedback>) {
    out << "current_waypoint: " << message.current_waypoint << "\n";

  // Five nav2 behaviors report progress with an empty message: there is nothing
  // to print, but saying so beats handing back the raw byte count.
  } else if constexpr (
    std::is_same_v<Message, nav2_msgs::action::SmoothPath::Feedback> ||
    std::is_same_v<Message, nav2_msgs::action::ComputePathThroughPoses::Feedback> ||
    std::is_same_v<Message, nav2_msgs::action::ComputeRoute::Feedback> ||
    std::is_same_v<Message, nav2_msgs::action::UndockRobot::Feedback> ||
    std::is_same_v<Message, nav2_msgs::action::DummyBehavior::Feedback>)
  {
    out << "(no fields)\n";
  } else {
    out << "(no pretty printer registered for this action type)\n";
  }
  return out.str();
}

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
    if (!setScalar(goal.EXPR, value)) { \
      error = "invalid value for " #EXPR ": '" + value + "'"; \
      return false; \
    } \
  }

// The seven fields of a `geometry_msgs/PoseStamped`, which two of the goals
// below carry as a nested message: the goal pose of NavigateToPose and both
// ends of ComputePathToPose. Spelled out once so the three call sites cannot
// drift apart.
#define ASSIGN_POSE(PREFIX) \
  ASSIGN(PREFIX.header.frame_id) \
  ASSIGN(PREFIX.header.stamp.sec) \
  ASSIGN(PREFIX.header.stamp.nanosec) \
  ASSIGN(PREFIX.pose.position.x) \
  ASSIGN(PREFIX.pose.position.y) \
  ASSIGN(PREFIX.pose.position.z) \
  ASSIGN(PREFIX.pose.orientation.x) \
  ASSIGN(PREFIX.pose.orientation.y) \
  ASSIGN(PREFIX.pose.orientation.z) \
  ASSIGN(PREFIX.pose.orientation.w)

// The three fields a behavior that drives or spins takes, shared by BackUp and
// DriveOnHeading because upstream gives the two actions the same goal message.
#define ASSIGN_HEADING_GOAL \
  ASSIGN(target.x) \
  ASSIGN(target.y) \
  ASSIGN(target.z) \
  ASSIGN(speed) \
  ASSIGN(time_allowance.sec) \
  ASSIGN(time_allowance.nanosec) \
  ASSIGN(disable_collision_checks)

// One waypoint out of a goal's list: the paths `ASSIGN_POSE` above spells out,
// applied to one element of a `geometry_msgs/PoseStamped[]` instead of to the
// goal's own pose. `poses[0].pose.position.x` and `pose.position.x` are
// deliberately the same spelling - an element is written the way the message is
// written when it stands alone.
inline bool applyPoseElement(
  geometry_msgs::msg::PoseStamped & element, const std::string & path, const std::string & value,
  std::string & error)
{
  auto assign = [&](auto & target) {
      if (setScalar(target, value)) {
        return true;
      }
      error = "invalid value for '" + path + "': '" + value + "'";
      return false;
    };
  if (path == "header.frame_id") {return assign(element.header.frame_id);}
  if (path == "header.stamp.sec") {return assign(element.header.stamp.sec);}
  if (path == "header.stamp.nanosec") {return assign(element.header.stamp.nanosec);}
  if (path == "pose.position.x") {return assign(element.pose.position.x);}
  if (path == "pose.position.y") {return assign(element.pose.position.y);}
  if (path == "pose.position.z") {return assign(element.pose.position.z);}
  if (path == "pose.orientation.x") {return assign(element.pose.orientation.x);}
  if (path == "pose.orientation.y") {return assign(element.pose.orientation.y);}
  if (path == "pose.orientation.z") {return assign(element.pose.orientation.z);}
  if (path == "pose.orientation.w") {return assign(element.pose.orientation.w);}
  error = "a waypoint has no settable field '" + path + "'";
  return false;
}

// The waypoints of a goal, one element at a time: `poses[0].pose.position.x`,
// `goals[1].pose.orientation.w`, `poses.goals[2].pose.position.x`. The list
// grows to hold the index, so the elements may be written in any order, and an
// element is filled by the same list of paths the goal's own pose uses.
#define ASSIGN_SEQ_ELEMENT(EXPR) \
  if (indexed.indexed && indexed.prefix == #EXPR) { \
    handled = true; \
    if (indexed.index >= goal.EXPR.size()) { \
      goal.EXPR.resize(indexed.index + 1); \
    } \
    if (!applyPoseElement(goal.EXPR[indexed.index], indexed.rest, value, error)) { \
      return false; \
    } \
  }

// A waypoint list written without an index - `poses=...`, which is what a
// caller who has not read this file would try. `[]` is an empty list and is
// taken; anything else says how to spell one element.
#define ASSIGN_SEQ_HINT(EXPR) \
  if (path == #EXPR) { \
    handled = true; \
    if (value.empty()) { \
      goal.EXPR.clear(); \
    } else { \
      error = "'" #EXPR "' carries waypoints, one per element: write '" #EXPR \
        "[0].pose.position.x' and so on"; \
      return false; \
    } \
  }

// The goal of an action, from the `field=value` assignments of the command
// line. A typo is an error rather than a silently ignored argument, exactly as
// in service_registry's request half.
//
// A goal that carries waypoints - `poses` (FollowWaypoints), `goals`
// (ComputePathThroughPoses), `poses.goals` (NavigateThroughPoses) and
// `path.poses` (FollowPath, SmoothPath) - takes them one element at a time,
// `poses[0].pose.position.x` and so on (see ASSIGN_SEQ_ELEMENT), which is the
// spelling `ros2 topic pub` uses for a `nav_msgs/msg/Path` as well. The
// remaining sequences (`viapoints` of ComputePathToPose, `gps_poses` of
// FollowGPSWaypoints) have the same shape and are not filled here yet.
template<class Goal>
bool applyGoalFields(
  Goal & goal, const std::vector<FieldAssign> & fields, std::string & error)
{
  for (const auto & field : fields) {
    const std::string & path = field.path;
    const std::string & value = field.value;
    const IndexedPath indexed = splitIndex(path);
    bool handled = false;
    if constexpr (std::is_same_v<Goal, ros2_bridge_sim::action::Drive::Goal>) {
      ASSIGN(distance)
    } else if constexpr (std::is_same_v<Goal, nav2_msgs::action::Spin::Goal>) {
      ASSIGN(target_yaw)
      ASSIGN(time_allowance.sec)
      ASSIGN(time_allowance.nanosec)
      ASSIGN(disable_collision_checks)
    } else if constexpr (
      std::is_same_v<Goal, nav2_msgs::action::BackUp::Goal> ||
      std::is_same_v<Goal, nav2_msgs::action::DriveOnHeading::Goal>)
    {
      ASSIGN_HEADING_GOAL
    } else if constexpr (std::is_same_v<Goal, nav2_msgs::action::Wait::Goal>) {
      ASSIGN(time.sec)
      ASSIGN(time.nanosec)
    } else if constexpr (std::is_same_v<Goal, nav2_msgs::action::NavigateToPose::Goal>) {
      ASSIGN_POSE(pose)
      ASSIGN(behavior_tree)
    } else if constexpr (std::is_same_v<Goal, nav2_msgs::action::ComputePathToPose::Goal>) {
      ASSIGN_POSE(goal)
      ASSIGN_POSE(start)
      ASSIGN(planner_id)
      ASSIGN(use_start)
    } else if constexpr (std::is_same_v<Goal, nav2_msgs::action::FollowPath::Goal>) {
      ASSIGN(path.header.frame_id)
      ASSIGN(path.header.stamp.sec)
      ASSIGN(path.header.stamp.nanosec)
      ASSIGN(controller_id)
      ASSIGN(goal_checker_id)
      ASSIGN(progress_checker_id)
      ASSIGN(path_handler_id)
      ASSIGN_SEQ_ELEMENT(path.poses)
      ASSIGN_SEQ_HINT(path.poses)
    } else if constexpr (std::is_same_v<Goal, nav2_msgs::action::FollowWaypoints::Goal> ||
      std::is_same_v<Goal, nav2_msgs::action::FollowGPSWaypoints::Goal>)
    {
      // The two waypoint followers: the loop count and the index / number of
      // the waypoint to resume at. FollowWaypoints then carries a
      // `geometry_msgs/PoseStamped[]`; FollowGPSWaypoints carries a `GeoPose[]`
      // instead, whose elements are not filled here yet (see the note above).
      ASSIGN(number_of_loops)
      ASSIGN(goal_index)
      if constexpr (std::is_same_v<Goal, nav2_msgs::action::FollowWaypoints::Goal>) {
        ASSIGN_SEQ_ELEMENT(poses)
        ASSIGN_SEQ_HINT(poses)
      }
    } else if constexpr (std::is_same_v<Goal, nav2_msgs::action::AssistedTeleop::Goal>) {
      ASSIGN(time_allowance.sec)
      ASSIGN(time_allowance.nanosec)
    } else if constexpr (std::is_same_v<Goal, nav2_msgs::action::SmoothPath::Goal>) {
      ASSIGN(path.header.frame_id)
      ASSIGN(path.header.stamp.sec)
      ASSIGN(path.header.stamp.nanosec)
      ASSIGN(smoother_id)
      ASSIGN(max_smoothing_duration.sec)
      ASSIGN(max_smoothing_duration.nanosec)
      ASSIGN(check_for_collisions)
      ASSIGN_SEQ_ELEMENT(path.poses)
      ASSIGN_SEQ_HINT(path.poses)
    } else if constexpr (
      std::is_same_v<Goal, nav2_msgs::action::ComputeRoute::Goal> ||
      std::is_same_v<Goal, nav2_msgs::action::ComputeAndTrackRoute::Goal>)
    {
      // Route computes one route, ComputeAndTrackRoute keeps following it.
      // Upstream gives the two goals the same message, so they share a branch.
      ASSIGN(start_id)
      ASSIGN_POSE(start)
      ASSIGN(goal_id)
      ASSIGN_POSE(goal)
      ASSIGN(use_start)
      ASSIGN(use_poses)
    } else if constexpr (
      std::is_same_v<Goal, nav2_msgs::action::ComputePathThroughPoses::Goal>)
    {
      // `goals` is a `nav_msgs/msg/Goals` - a header and the waypoint list -
      // and `start` is one pose.
      ASSIGN(goals.header.frame_id)
      ASSIGN_SEQ_ELEMENT(goals.goals)
      ASSIGN_SEQ_HINT(goals.goals)
      ASSIGN_POSE(start)
      ASSIGN(planner_id)
      ASSIGN(use_start)
    } else if constexpr (std::is_same_v<Goal, nav2_msgs::action::NavigateThroughPoses::Goal>) {
      // `poses` is a `nav_msgs/msg/Goals` - a header and the waypoint list -
      // and `behavior_tree` is one string.
      ASSIGN(behavior_tree)
      ASSIGN(poses.header.frame_id)
      ASSIGN_SEQ_ELEMENT(poses.goals)
      ASSIGN_SEQ_HINT(poses.goals)
    } else if constexpr (std::is_same_v<Goal, nav2_msgs::action::DockRobot::Goal>) {
      ASSIGN(use_dock_id)
      ASSIGN(dock_id)
      ASSIGN_POSE(dock_pose)
      ASSIGN(dock_type)
      ASSIGN(max_staging_time)
      ASSIGN(navigate_to_staging_pose)
    } else if constexpr (std::is_same_v<Goal, nav2_msgs::action::UndockRobot::Goal>) {
      ASSIGN(dock_type)
      ASSIGN(max_undocking_time)
    } else if constexpr (std::is_same_v<Goal, nav2_msgs::action::FollowObject::Goal>) {
      ASSIGN(pose_topic)
      ASSIGN(tracked_frame)
      ASSIGN(max_duration.sec)
      ASSIGN(max_duration.nanosec)
    } else if constexpr (std::is_same_v<Goal, nav2_msgs::action::DummyBehavior::Goal>) {
      // The goal is one `std_msgs/String`; `data` is its only field.
      ASSIGN(command.data)
    } else {
      (void)goal;
      (void)value;
    }
    if (!handled) {
      error = "action type '" + std::string(Goal::ros2_message_type) +
        "' has no settable field '" + path + "'";
      return false;
    }
  }
  return true;
}

#undef ASSIGN_HEADING_GOAL
#undef ASSIGN_SEQ_ELEMENT
#undef ASSIGN_SEQ_HINT
#undef ASSIGN_POSE
#undef ASSIGN

// ------------------------------------------------------------- entry point

// Everything the bridge knows about one action type. The three conversions are
// the very same template pair the topic and service registries use - an action
// part *is* a message, `.../action/Drive_Goal`, which is why there is no
// action specific transcoding anywhere.
template<class ActionT>
ActionCodec makeAction(const char * type)
{
  ActionCodec codec;
  codec.type = type;
  codec.goal_type = ActionT::Goal::ros2_message_type;
  codec.feedback_type = ActionT::Feedback::ros2_message_type;
  codec.result_type = ActionT::Result::ros2_message_type;
  codec.goal_to_cdr = &shimToCdr<typename ActionT::Goal>;
  codec.feedback_to_shim = &cdrToShim<typename ActionT::Feedback>;
  codec.result_to_shim = &cdrToShim<typename ActionT::Result>;
  codec.default_result = ros2_shim::serialize(typename ActionT::Result{});
  codec.describe_goal = &describeMessage<typename ActionT::Goal>;
  codec.describe_feedback = &describeMessage<typename ActionT::Feedback>;
  codec.describe_result = &describeMessage<typename ActionT::Result>;
  codec.build_goal = [](const std::vector<FieldAssign> & fields, Bytes & shim, std::string & error) {
      typename ActionT::Goal goal{};
      if (!applyGoalFields(goal, fields, error)) {
        return false;
      }
      shim = ros2_shim::serialize(goal);
      return true;
    };
  return codec;
}

const std::map<std::string, ActionCodec> & registry()
{
  static const std::map<std::string, ActionCodec> table = {
    {"ros2_bridge_sim/action/Drive",
      makeAction<ros2_bridge_sim::action::Drive>("ros2_bridge_sim/action/Drive")},

    // The behaviors, in the order nav2_behaviors' own plugin list has them.
    {"nav2_msgs/action/Spin",
      makeAction<nav2_msgs::action::Spin>("nav2_msgs/action/Spin")},
    {"nav2_msgs/action/BackUp",
      makeAction<nav2_msgs::action::BackUp>("nav2_msgs/action/BackUp")},
    {"nav2_msgs/action/DriveOnHeading",
      makeAction<nav2_msgs::action::DriveOnHeading>("nav2_msgs/action/DriveOnHeading")},
    {"nav2_msgs/action/Wait",
      makeAction<nav2_msgs::action::Wait>("nav2_msgs/action/Wait")},

    // The navigator and its two halves.
    {"nav2_msgs/action/NavigateToPose",
      makeAction<nav2_msgs::action::NavigateToPose>("nav2_msgs/action/NavigateToPose")},
    {"nav2_msgs/action/ComputePathToPose",
      makeAction<nav2_msgs::action::ComputePathToPose>(
        "nav2_msgs/action/ComputePathToPose")},
    {"nav2_msgs/action/FollowPath",
      makeAction<nav2_msgs::action::FollowPath>("nav2_msgs/action/FollowPath")},
    {"nav2_msgs/action/FollowWaypoints",
      makeAction<nav2_msgs::action::FollowWaypoints>("nav2_msgs/action/FollowWaypoints")},
    {"nav2_msgs/action/NavigateThroughPoses",
      makeAction<nav2_msgs::action::NavigateThroughPoses>(
        "nav2_msgs/action/NavigateThroughPoses")},
    {"nav2_msgs/action/SmoothPath",
      makeAction<nav2_msgs::action::SmoothPath>("nav2_msgs/action/SmoothPath")},

    // The second planner entry point (ComputePathThroughPoses) and the route
    // server's pair. `ComputeAndTrackRoute` and `ComputeRoute` take the same
    // goal message, so they are two rows over one branch above.
    {"nav2_msgs/action/ComputePathThroughPoses",
      makeAction<nav2_msgs::action::ComputePathThroughPoses>(
        "nav2_msgs/action/ComputePathThroughPoses")},
    {"nav2_msgs/action/ComputeRoute",
      makeAction<nav2_msgs::action::ComputeRoute>("nav2_msgs/action/ComputeRoute")},
    {"nav2_msgs/action/ComputeAndTrackRoute",
      makeAction<nav2_msgs::action::ComputeAndTrackRoute>(
        "nav2_msgs/action/ComputeAndTrackRoute")},

    // Docking, object following and teleop.
    {"nav2_msgs/action/DockRobot",
      makeAction<nav2_msgs::action::DockRobot>("nav2_msgs/action/DockRobot")},
    {"nav2_msgs/action/UndockRobot",
      makeAction<nav2_msgs::action::UndockRobot>("nav2_msgs/action/UndockRobot")},
    {"nav2_msgs/action/FollowObject",
      makeAction<nav2_msgs::action::FollowObject>("nav2_msgs/action/FollowObject")},
    {"nav2_msgs/action/FollowGPSWaypoints",
      makeAction<nav2_msgs::action::FollowGPSWaypoints>(
        "nav2_msgs/action/FollowGPSWaypoints")},
    {"nav2_msgs/action/AssistedTeleop",
      makeAction<nav2_msgs::action::AssistedTeleop>("nav2_msgs/action/AssistedTeleop")},
    {"nav2_msgs/action/DummyBehavior",
      makeAction<nav2_msgs::action::DummyBehavior>("nav2_msgs/action/DummyBehavior")},
  };
  return table;
}

}  // namespace

std::string canonical_action_type(const std::string & type_name)
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
  // `nav2_msgs/NavigateToPose` and `nav2_msgs::action::NavigateToPose` are both
  // spellings a caller may produce; the canonical one carries the kind.
  return type_name.substr(0, first) + "/action/" + type_name.substr(first + 1);
}

std::string action_goal_type(const std::string & action_type)
{
  return canonical_action_type(action_type) + "_Goal";
}

std::string action_feedback_type(const std::string & action_type)
{
  return canonical_action_type(action_type) + "_Feedback";
}

std::string action_result_type(const std::string & action_type)
{
  return canonical_action_type(action_type) + "_Result";
}

const ActionCodec * find_action_codec(const std::string & type_name)
{
  const auto & table = registry();
  const auto it = table.find(canonical_action_type(type_name));
  return it == table.end() ? nullptr : &it->second;
}

bool is_supported_action(const std::string & type_name)
{
  return find_action_codec(type_name) != nullptr;
}

std::vector<std::string> supported_actions()
{
  std::vector<std::string> names;
  for (const auto & entry : registry()) {
    names.push_back(entry.first);
  }
  return names;
}

ShimResultCode shim_result_code(ros2_grpc_sdk::GoalStatus status)
{
  switch (status) {
    case ros2_grpc_sdk::GoalStatus::Succeeded:
      return ShimResultCode::Succeeded;
    case ros2_grpc_sdk::GoalStatus::Canceled:
      return ShimResultCode::Canceled;
    case ros2_grpc_sdk::GoalStatus::Aborted:
    // A rejected goal is answered on the goal-ack channel and never reaches a
    // result; if a peer sends one anyway it is a failure, not a success.
    case ros2_grpc_sdk::GoalStatus::Rejected:
      return ShimResultCode::Aborted;
    default:
      return ShimResultCode::Unknown;
  }
}

}  // namespace ros2_bridge
