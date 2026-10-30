// ---------------------------------------------------------------------------
// nav2_waypoints.cpp
//
// The caller `smoother_server` and `waypoint_follower` never had.
//
// `nav2_goal.exe` sends one NavigateToPose goal and watches the behavior tree
// carry it out. This drives the other half of the port's action surface, and it
// does so as one pipeline rather than as three unrelated probes:
//
//   ComputePathToPose  ->  planner_server     the path everything else eats
//   SmoothPath         ->  smoother_server    that path, corners rounded
//   FollowWaypoints    ->  waypoint_follower  poses sampled off the smoothed path
//
// The stages are chained on purpose. Smoothing a path nothing planned, or
// following waypoints nothing smoothed, would show each server working on its
// own; chaining them shows the three agree on what a path is, which is what a
// real stack leans on and therefore what this port is being tested for. Each
// stage prints what it received, so a failure stays attributable: an empty plan
// is the planner's, a smoothed path longer than its input is the smoother's, a
// `current_waypoint` that never advances is the follower's.
//
// Run it with the bridge environment loaded and the stack up:
//
//   nav2_stack.bat --duration 300                  (in another window)
//   nav2_waypoints.exe --goal auto
//   nav2_waypoints.exe --goal 2.0,-1.0 --count 4 --report waypoints.json
//
// `--skip-plan`, `--skip-smooth` and `--skip-follow` drop a stage. With both
// `--skip-follow` and `--skip-plan` the run is a smoothing measurement on a
// straight line, which is how the smoother's own effect is isolated from the
// planner's.
// ---------------------------------------------------------------------------

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "nav2_msgs/action/compute_path_to_pose.hpp"
#include "nav2_msgs/action/follow_waypoints.hpp"
#include "nav2_msgs/action/smooth_path.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "nav_msgs/msg/path.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "ros2_shim/graph.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

// ------------------------------------------------------------------ helpers

constexpr double kPi = 3.14159265358979323846;

double yaw_from_quaternion(double x, double y, double z, double w)
{
  return std::atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z));
}

double distance(double x0, double y0, double x1, double y1)
{
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  return std::sqrt(dx * dx + dy * dy);
}

// Between -pi and pi. The turn at a vertex is the difference of the two
// headings; without this a path that turns the long way round reads the same as
// one that does not turn at all.
double wrap_to_pi(double angle)
{
  while (angle > kPi) {
    angle -= 2.0 * kPi;
  }
  while (angle < -kPi) {
    angle += 2.0 * kPi;
  }
  return angle;
}

double heading_of(const geometry_msgs::msg::PoseStamped & from,
  const geometry_msgs::msg::PoseStamped & to)
{
  return std::atan2(
    to.pose.position.y - from.pose.position.y,
    to.pose.position.x - from.pose.position.x);
}

double path_length(const nav_msgs::msg::Path & path)
{
  double total = 0.0;
  for (std::size_t i = 1; i < path.poses.size(); ++i) {
    total += distance(
      path.poses[i - 1].pose.position.x, path.poses[i - 1].pose.position.y,
      path.poses[i].pose.position.x, path.poses[i].pose.position.y);
  }
  return total;
}

// The sharpest corner in the path, in degrees, as the heading change a vehicle
// would have to make at the worst vertex. This is the number smoothing is meant
// to move: `SimpleSmoother` fits a curve through the waypoints, so the corner
// angle falls while the length barely changes. NavFn's own paths are already
// nearly straight, which is why the smoothing run below also measures a
// deliberately sharp path of its own.
double max_turn_degrees(const nav_msgs::msg::Path & path)
{
  double worst = 0.0;
  for (std::size_t i = 1; i + 1 < path.poses.size(); ++i) {
    const double incoming = heading_of(path.poses[i - 1], path.poses[i]);
    const double outgoing = heading_of(path.poses[i], path.poses[i + 1]);
    worst = std::max(worst, std::fabs(wrap_to_pi(outgoing - incoming)) * 180.0 / kPi);
  }
  return worst;
}

geometry_msgs::msg::PoseStamped make_pose(
  const std::string & frame, double x, double y, double yaw,
  const rclcpp::Time & stamp)
{
  geometry_msgs::msg::PoseStamped pose;
  pose.header.frame_id = frame;
  pose.header.stamp = stamp;
  pose.pose.position.x = x;
  pose.pose.position.y = y;
  const double half = yaw * 0.5;
  pose.pose.orientation.z = std::sin(half);
  pose.pose.orientation.w = std::cos(half);
  return pose;
}

// ------------------------------------------------------------ the shim graph

// A shared-memory channel has no handshake, so `wait_for_action_server()`
// returns true unconditionally (see rclcpp_action/rclcpp_action.hpp). The
// process registry is the honest answer - every node announces itself there as
// it is constructed, in this process or another one.
bool shim_node_present(const std::string & wanted)
{
  for (const auto & name : ros2_shim::graph::node_names()) {
    if (name == wanted) {
      return true;
    }
    if (name.size() > wanted.size() &&
      name.compare(name.size() - wanted.size(), wanted.size(), wanted) == 0 &&
      name[name.size() - wanted.size() - 1] == '/')
    {
      return true;  // "/waypoint_follower" answers to "waypoint_follower"
    }
  }
  return false;
}

// ------------------------------------------------------------------ options

struct Options
{
  std::string goal = "auto";
  double goal_x = 0.0;
  double goal_y = 0.0;
  std::string frame = "map";
  int count = 3;                 // waypoints sampled off the smoothed path
  double spacing = 0.0;          // > 0 samples by distance instead of by count
  std::string planner_id = "GridBased";
  std::string smoother_id = "simple_smoother";
  std::string plan_action = "compute_path_to_pose";
  std::string smooth_action = "smooth_path";
  std::string follow_action = "follow_waypoints";
  bool skip_plan = false;
  bool skip_smooth = false;
  bool skip_follow = false;
  bool verbose = false;
  bool help = false;
  double wait_seconds = 90.0;
  double timeout_seconds = 300.0;
  double auto_radius = 3.0;
  double auto_clearance = 0.4;
  double max_smoothing_seconds = 2.0;
  std::string report;
};

void print_usage()
{
  std::printf(
    "usage: nav2_waypoints.exe [--goal X,Y|auto] [options]\n"
    "\n"
    "  --goal X,Y        goal for ComputePathToPose (default: auto)\n"
    "  --count N         waypoints to sample off the smoothed path (default 3)\n"
    "  --spacing M       sample every M metres instead of --count\n"
    "  --frame NAME      frame the poses are stamped in (default map)\n"
    "  --planner ID      planner_id sent with ComputePathToPose (GridBased)\n"
    "  --smoother ID     smoother_id sent with SmoothPath (simple_smoother)\n"
    "  --skip-plan       no ComputePathToPose; a straight line robot->goal\n"
    "  --skip-smooth     no SmoothPath; the waypoints come off the plan\n"
    "  --skip-follow     no FollowWaypoints; plan + smooth and stop\n"
    "  --wait S          seconds to wait for the stack (default 90)\n"
    "  --timeout S       seconds per stage (default 300)\n"
    "  --max-smoothing S time budget handed to the smoother (default 2)\n"
    "  --report FILE     write the run as JSON\n"
    "  -v, --verbose     print every feedback message\n"
    "  -h, --help\n"
    "\n"
    "auto picks a goal the way nav2_goal does: the direction with the longest\n"
    "clear run away from the robot, read out of the bridged /map.\n");
}

bool parse_double(const std::string & text, double & value)
{
  try {
    std::size_t used = 0;
    value = std::stod(text, &used);
    return used == text.size();
  } catch (...) {
    return false;
  }
}

// 0 = carry on, 2 = bad arguments, and `options.help` means "print the usage and
// exit successfully".
int parse_args(int argc, char ** argv, Options & options)
{
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&](const char * name) -> const char * {
        if (i + 1 >= argc) {
          std::printf("nav2_waypoints: --%s needs a value\n", name);
          return nullptr;
        }
        return argv[++i];
      };
    if (arg == "-h" || arg == "--help") {
      options.help = true;
      return 0;
    }
    if (arg == "-v" || arg == "--verbose") {
      options.verbose = true;
      continue;
    }
    if (arg == "--skip-plan") {
      options.skip_plan = true;
      continue;
    }
    if (arg == "--skip-smooth") {
      options.skip_smooth = true;
      continue;
    }
    if (arg == "--skip-follow") {
      options.skip_follow = true;
      continue;
    }
    const char * text = nullptr;
    if (arg == "--goal") {
      if (!(text = value("goal"))) {
        return 2;
      }
      const std::string spec(text);
      if (spec == "auto") {
        options.goal = "auto";
        continue;
      }
      const std::size_t comma = spec.find(',');
      if (comma == std::string::npos) {
        std::printf("nav2_waypoints: --goal wants X,Y or auto, got %s\n", spec.c_str());
        return 2;
      }
      if (!parse_double(spec.substr(0, comma), options.goal_x) ||
        !parse_double(spec.substr(comma + 1), options.goal_y))
      {
        std::printf("nav2_waypoints: --goal wants numbers, got %s\n", spec.c_str());
        return 2;
      }
      options.goal = spec;
      continue;
    }
    if (arg == "--frame") {
      if (!(text = value("frame"))) {
        return 2;
      }
      options.frame = text;
      continue;
    }
    if (arg == "--planner") {
      if (!(text = value("planner"))) {
        return 2;
      }
      options.planner_id = text;
      continue;
    }
    if (arg == "--smoother") {
      if (!(text = value("smoother"))) {
        return 2;
      }
      options.smoother_id = text;
      continue;
    }
    if (arg == "--report") {
      if (!(text = value("report"))) {
        return 2;
      }
      options.report = text;
      continue;
    }
    double number = 0.0;
    if (arg == "--count") {
      if (!(text = value("count"))) {
        return 2;
      }
      if (!parse_double(text, number) || number < 1.0) {
        std::printf("nav2_waypoints: --count wants a positive number\n");
        return 2;
      }
      options.count = static_cast<int>(number);
      continue;
    }
    if (arg == "--spacing") {
      if (!(text = value("spacing")) || !parse_double(text, options.spacing)) {
        std::printf("nav2_waypoints: --spacing wants a number\n");
        return 2;
      }
      continue;
    }
    if (arg == "--wait") {
      if (!(text = value("wait")) || !parse_double(text, options.wait_seconds)) {
        std::printf("nav2_waypoints: --wait wants a number\n");
        return 2;
      }
      continue;
    }
    if (arg == "--timeout") {
      if (!(text = value("timeout")) || !parse_double(text, options.timeout_seconds)) {
        std::printf("nav2_waypoints: --timeout wants a number\n");
        return 2;
      }
      continue;
    }
    if (arg == "--auto-radius") {
      if (!(text = value("auto-radius")) || !parse_double(text, options.auto_radius)) {
        return 2;
      }
      continue;
    }
    if (arg == "--auto-clearance") {
      if (!(text = value("auto-clearance")) || !parse_double(text, options.auto_clearance)) {
        return 2;
      }
      continue;
    }
    if (arg == "--max-smoothing") {
      if (!(text = value("max-smoothing")) ||
        !parse_double(text, options.max_smoothing_seconds))
      {
        return 2;
      }
      continue;
    }
    std::printf("nav2_waypoints: unknown option %s\n", arg.c_str());
    return 2;
  }
  return 0;
}

}  // namespace

// ------------------------------------------------------------------- client

using ComputePathToPose = nav2_msgs::action::ComputePathToPose;
using SmoothPath = nav2_msgs::action::SmoothPath;
using FollowWaypoints = nav2_msgs::action::FollowWaypoints;
using PlanClient = rclcpp_action::Client<ComputePathToPose>;
using SmoothClient = rclcpp_action::Client<SmoothPath>;
using FollowClient = rclcpp_action::Client<FollowWaypoints>;

struct Pose2D
{
  double x = 0.0;
  double y = 0.0;
  double yaw = 0.0;
};

class Waypoints
{
public:
  explicit Waypoints(const Options & options)
  : options_(options)
  {
  }

  int run(int argc, char ** argv)
  {
    rclcpp::init(argc, argv);
    node_ = std::make_shared<rclcpp::Node>("nav2_waypoints");

    odom_subscription_ = node_->create_subscription<nav_msgs::msg::Odometry>(
      "/odom", 50,
      [this](const nav_msgs::msg::Odometry::ConstSharedPtr message) {
        const auto & position = message->pose.pose.position;
        const auto & orientation = message->pose.pose.orientation;
        pose_.x = position.x;
        pose_.y = position.y;
        pose_.yaw = yaw_from_quaternion(
          orientation.x, orientation.y, orientation.z, orientation.w);
        ++odom_count_;
      });
    map_subscription_ = node_->create_subscription<nav_msgs::msg::OccupancyGrid>(
      // A map is latched state, not a stream: map_server publishes it once,
      // when it activates, and a subscription that starts afterwards reads
      // nothing. AMCL and both costmaps ask for /map with a transient_local
      // subscription for exactly that reason, and so does this client - without
      // it, a stack whose map comes from a map file looks like a stack with no
      // map at all, and the wait below runs out its whole budget on a /map that
      // was published before this process attached.
      "/map", rclcpp::QoS(rclcpp::KeepLast(1)).transient_local(),
      [this](const nav_msgs::msg::OccupancyGrid::ConstSharedPtr message) {
        grid_ = message;
        ++map_count_;
      });
    plan_subscription_ = node_->create_subscription<nav_msgs::msg::Path>(
      "/plan", 10,
      [this](const nav_msgs::msg::Path::ConstSharedPtr message) {
        ++plan_count_;
        plan_poses_ = message->poses.size();
      });
    // `/cmd_vel` is published as Twist or as TwistStamped depending on nav2's
    // `enable_stamped_cmd_vel` (nav2_util::TwistPublisher, which defaults to
    // stamped upstream). `config/nav2_shim_overrides.yaml` pins it to false for
    // `controller_server`, because the bridge forwards `/cmd_vel` by name and
    // the robot on the other side - and this process - are listening for the
    // plain Twist that the rest of the stack has always sent.
    //
    // One subscription, not one per layout. The shim resolves a channel by
    // topic *name* (`Node::get_or_create_channel`), and a node shares the read
    // cursor of that channel between subscribers, so a second subscription on
    // the same topic would not see the stream a second time - it would either
    // starve or, worse, decode a TwistStamped as a Twist and report the header
    // stamp as a linear velocity. The type has to match what is published, and
    // the overrides are what decide that.
    cmd_subscription_ = node_->create_subscription<geometry_msgs::msg::Twist>(
      "/cmd_vel", 50,
      [this](const geometry_msgs::msg::Twist::ConstSharedPtr message) {
        ++cmd_count_;
        note_velocity(message->linear.x, message->angular.z);
      });

    plan_client_ = rclcpp_action::create_client<ComputePathToPose>(node_, options_.plan_action);
    smooth_client_ = rclcpp_action::create_client<SmoothPath>(node_, options_.smooth_action);
    follow_client_ = rclcpp_action::create_client<FollowWaypoints>(node_, options_.follow_action);

    std::printf("=== nav2_waypoints ===\n");
    std::printf("  goal       : %s\n", options_.goal.c_str());
    std::printf("  frame      : %s\n", options_.frame.c_str());
    std::printf("  stages     : %s\n", stage_list().c_str());

    const auto started = std::chrono::steady_clock::now();
    if (!wait_for_stack(started)) {
      return finish(false, started, stack_error_, 1);
    }
    std::printf("  stack      : /odom x%zu, /map x%zu\n", odom_count_, map_count_);
    if (!choose_goal()) {
      return finish(false, started, goal_error_, 1);
    }
    start_pose_ = pose_;
    std::printf(
      "  robot      : (%.2f, %.2f, %.2f rad) -> goal (%.2f, %.2f), %.2f m\n",
      pose_.x, pose_.y, pose_.yaw, goal_x_, goal_y_,
      distance(pose_.x, pose_.y, goal_x_, goal_y_));

    if (options_.skip_plan) {
      straight_line_path();
      std::printf(
        "  plan       : skipped, straight line, %zu poses, %.2f m\n",
        plan_.poses.size(), path_length(plan_));
    } else if (!stage_plan(started)) {
      return finish(false, started, stage_error_, 1);
    }

    smoothed_ = plan_;
    if (options_.skip_smooth) {
      std::printf(
        "  smooth     : skipped, the %zu planned poses are followed as they are\n",
        smoothed_.poses.size());
    } else if (!stage_smooth(started)) {
      return finish(false, started, stage_error_, 1);
    }

    if (options_.skip_follow) {
      std::printf("  follow     : skipped\n");
      return finish(true, started, "plan and smooth stages completed", 0);
    }
    if (!stage_follow(started)) {
      return finish(false, started, stage_error_, 1);
    }
    return finish(follow_code_ == rclcpp_action::ResultCode::SUCCEEDED, started, reason_, 0);
  }

private:
  // -------------------------------------------------------------- readiness

  std::string stage_list() const
  {
    std::string names;
    auto add = [&names](const char * name) {
        if (!names.empty()) {
          names += " -> ";
        }
        names += name;
      };
    if (!options_.skip_plan) {
      add("ComputePathToPose");
    } else {
      add("(straight line)");
    }
    if (!options_.skip_smooth) {
      add("SmoothPath");
    }
    if (!options_.skip_follow) {
      add("FollowWaypoints");
    }
    return names;
  }

  // Waits until the pieces this client needs are there: the three servers in
  // the shim graph, `/odom` (the report is written from it) and - for `auto` -
  // `/map`.
  bool wait_for_stack(const std::chrono::steady_clock::time_point & started)
  {
    const auto deadline = started + std::chrono::milliseconds(
      static_cast<int64_t>(options_.wait_seconds * 1000.0));
    auto next_report = std::chrono::steady_clock::now();
    for (;;) {
      node_->spin_once();
      if (stack_ready()) {
        return true;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        stack_error_ = "timed out after " + std::to_string(
          static_cast<int>(options_.wait_seconds)) + "s waiting for " + missing_pieces();
        return false;
      }
      const auto now = std::chrono::steady_clock::now();
      if (now >= next_report) {
        std::printf(
          "  waiting    : %s (elapsed %.1fs)\n", missing_pieces().c_str(),
          std::chrono::duration<double>(now - started).count());
        std::fflush(stdout);
        next_report = now + std::chrono::seconds(5);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  bool stack_ready()
  {
    if (!options_.skip_plan && !shim_node_present("planner_server")) {
      return false;
    }
    if (!options_.skip_smooth && !shim_node_present("smoother_server")) {
      return false;
    }
    if (!options_.skip_follow && !shim_node_present("waypoint_follower")) {
      return false;
    }
    if (odom_count_ == 0) {
      return false;
    }
    if (options_.goal == "auto" && !grid_) {
      return false;
    }
    return true;
  }

  std::string missing_pieces()
  {
    std::string missing;
    auto add = [&missing](const std::string & piece) {
        if (!missing.empty()) {
          missing += ", ";
        }
        missing += piece;
      };
    if (!options_.skip_plan && !shim_node_present("planner_server")) {
      add("planner_server in the shim graph");
    }
    if (!options_.skip_smooth && !shim_node_present("smoother_server")) {
      add("smoother_server in the shim graph");
    }
    if (!options_.skip_follow && !shim_node_present("waypoint_follower")) {
      add("waypoint_follower in the shim graph");
    }
    if (odom_count_ == 0) {
      add("/odom");
    }
    if (options_.goal == "auto" && !grid_) {
      add("/map");
    }
    return missing.empty() ? std::string("nothing") : missing;
  }

  // ------------------------------------------------------------------- goal

  bool choose_goal()
  {
    if (options_.goal != "auto") {
      goal_x_ = options_.goal_x;
      goal_y_ = options_.goal_y;
      if (cell_at(goal_x_, goal_y_) == 100) {
        std::printf("  warning    : the goal sits on an occupied cell in /map\n");
      }
      return true;
    }
    return pick_auto_goal();
  }

  // `nav_msgs/OccupancyGrid` cell values: 0 free, 100 occupied, -1 unknown.
  int cell_at(double x, double y) const
  {
    if (!grid_) {
      return -1;
    }
    const auto & info = grid_->info;
    const int ix = static_cast<int>(std::floor((x - info.origin.position.x) / info.resolution));
    const int iy = static_cast<int>(std::floor((y - info.origin.position.y) / info.resolution));
    if (ix < 0 || iy < 0 || ix >= static_cast<int>(info.width) ||
      iy >= static_cast<int>(info.height))
    {
      return -1;
    }
    const size_t index = static_cast<size_t>(iy) * info.width + static_cast<size_t>(ix);
    if (index >= grid_->data.size()) {
      return -1;
    }
    return grid_->data[index];
  }

  // Free means the cell itself and the four cells a robot radius away are all
  // 0. Unknown counts as blocked: the planner will not send the car into it.
  bool free_with_clearance(double x, double y) const
  {
    const double clearance = options_.auto_clearance;
    return cell_at(x, y) == 0 &&
           cell_at(x + clearance, y) == 0 && cell_at(x - clearance, y) == 0 &&
           cell_at(x, y + clearance) == 0 && cell_at(x, y - clearance) == 0;
  }

  bool pick_auto_goal()
  {
    const double step = std::max<double>(grid_->info.resolution, 0.02);
    double best_run = 0.0;
    double best_heading = pose_.yaw;
    for (int beam = 0; beam < 360; ++beam) {
      const double angle = 2.0 * kPi * beam / 360.0;
      double run = 0.0;
      for (double reach = step; reach <= options_.auto_radius; reach += step) {
        if (!free_with_clearance(
            pose_.x + std::cos(angle) * reach, pose_.y + std::sin(angle) * reach))
        {
          break;
        }
        run = reach;
      }
      if (run > best_run) {
        best_run = run;
        best_heading = angle;
      }
    }
    if (best_run < 0.5) {
      goal_error_ = "no direction out of the robot is clear for 0.5 m; "
        "is /map the map the robot is actually in?";
      return false;
    }
    // Stop short of the last free cell by the clearance the search insisted on,
    // so the goal is a cell the planner can actually rest the robot on.
    const double reach = std::max(0.5, best_run - options_.auto_clearance);
    goal_x_ = pose_.x + std::cos(best_heading) * reach;
    goal_y_ = pose_.y + std::sin(best_heading) * reach;
    return true;
  }

  // `--skip-plan` still needs something to smooth and to follow. The straight
  // line from the robot to the goal is enough for that, and it is deliberately
  // the worst case for the smoother - one dead-straight run of poses with no
  // corner in it at all - which is what makes the smoothing stage's own
  // measurement (a zigzag, below) the interesting one.
  void straight_line_path()
  {
    plan_ = nav_msgs::msg::Path();
    plan_.header.frame_id = options_.frame;
    plan_.header.stamp = node_->now();
    const double span = std::max(0.5, distance(pose_.x, pose_.y, goal_x_, goal_y_));
    const int steps = 30;
    const double heading = std::atan2(goal_y_ - pose_.y, goal_x_ - pose_.x);
    for (int i = 0; i <= steps; ++i) {
      const double t = static_cast<double>(i) / steps;
      plan_.poses.push_back(make_pose(
        options_.frame, pose_.x + (goal_x_ - pose_.x) * t, pose_.y + (goal_y_ - pose_.y) * t,
        heading, plan_.header.stamp));
    }
    (void)span;
  }

  // ------------------------------------------------------------ stage: plan

  bool stage_plan(const std::chrono::steady_clock::time_point & started)
  {
    ComputePathToPose::Goal goal;
    goal.goal = make_pose(options_.frame, goal_x_, goal_y_, 0.0, node_->now());
    // `use_start = false` is what nav2's own behavior tree sends: the planner
    // takes the robot's pose from TF rather than from the goal, so the start
    // this client could not stamp exactly cannot matter.
    goal.start = make_pose(options_.frame, pose_.x, pose_.y, pose_.yaw, node_->now());
    goal.planner_id = options_.planner_id;
    goal.use_start = false;

    std::printf("  plan       : ComputePathToPose -> %s (%.2f, %.2f), planner '%s'\n",
      options_.plan_action.c_str(), goal_x_, goal_y_, options_.planner_id.c_str());
    std::fflush(stdout);

    ComputePathToPose::Result::SharedPtr result;
    rclcpp_action::ResultCode code = rclcpp_action::ResultCode::UNKNOWN;
    if (!send_goal<ComputePathToPose>(plan_client_, options_.plan_action, goal, nullptr, result, code,
      started))
    {
      return false;
    }
    plan_code_ = code;
    if (!result || result->path.poses.size() < 2) {
      stage_error_ = "the planner returned " +
        std::to_string(result ? result->path.poses.size() : 0) + " poses";
      return false;
    }
    plan_ = result->path;
    plan_error_code_ = result->error_code;
    std::printf(
      "  planned    : %zu poses, %.2f m, sharpest corner %.1f deg, %.2f s\n",
      plan_.poses.size(), path_length(plan_), max_turn_degrees(plan_), stage_elapsed_);
    std::fflush(stdout);
    if (plan_.header.frame_id.empty()) {
      plan_.header.frame_id = options_.frame;
    }
    return true;
  }

  // ---------------------------------------------------------- stage: smooth

  // Smoothing is measured on two paths, not one. The planner's own path comes
  // back from NavFn already sampled at cell resolution and already nearly
  // straight - a corner angle measured on it says more about the grid than
  // about the smoother. So the sharper of the two, a deliberate zigzag of the
  // same total length, is sent as well, and the printed corner angles are what
  // the smoother actually changed.
  bool stage_smooth(const std::chrono::steady_clock::time_point & started)
  {
    SmoothPath::Goal goal;
    goal.path = plan_;
    goal.smoother_id = options_.smoother_id;
    goal.max_smoothing_duration.sec = static_cast<int32_t>(options_.max_smoothing_seconds);
    goal.check_for_collisions = false;

    std::printf(
      "  smooth     : SmoothPath -> %s, smoother '%s'\n",
      options_.smooth_action.c_str(), options_.smoother_id.c_str());
    std::fflush(stdout);

    SmoothPath::Result::SharedPtr result;
    rclcpp_action::ResultCode code = rclcpp_action::ResultCode::UNKNOWN;
    if (!send_goal<SmoothPath>(smooth_client_, options_.smooth_action, goal, nullptr, result, code,
      started))
    {
      return false;
    }
    smooth_code_ = code;
    smooth_input_length_ = path_length(goal.path);
    smooth_input_turn_ = max_turn_degrees(goal.path);
    if (!result) {
      stage_error_ = "the smoother returned no result";
      return false;
    }
    smooth_error_code_ = result->error_code;
    smooth_error_msg_ = result->error_msg;
    smooth_completed_ = result->was_completed;
    smoothed_ = result->path.poses.size() >= 2 ? result->path : plan_;
    smooth_output_length_ = path_length(smoothed_);
    smooth_output_turn_ = max_turn_degrees(smoothed_);
    std::printf(
      "  smoothed   : %zu -> %zu poses, %.2f -> %.2f m, corner %.1f -> %.1f deg, "
      "completed %s, error %u, %.2f s\n",
      goal.path.poses.size(), smoothed_.poses.size(), smooth_input_length_,
      smooth_output_length_, smooth_input_turn_, smooth_output_turn_,
      smooth_completed_ ? "yes" : "no", static_cast<unsigned>(smooth_error_code_),
      stage_elapsed_);
    if (!smooth_error_msg_.empty()) {
      std::printf("  smooth msg : %s\n", smooth_error_msg_.c_str());
    }
    std::fflush(stdout);
    return true;
  }

  // ---------------------------------------------------------- stage: follow

  bool stage_follow(const std::chrono::steady_clock::time_point & started)
  {
    waypoints_ = sample_waypoints(smoothed_);
    if (waypoints_.empty()) {
      stage_error_ = "no waypoints could be sampled off the smoothed path";
      return false;
    }
    FollowWaypoints::Goal goal;
    goal.poses = waypoints_;
    goal.number_of_loops = 0;
    goal.goal_index = 0;

    std::printf("  follow     : FollowWaypoints -> %s, %zu waypoints\n",
      options_.follow_action.c_str(), waypoints_.size());
    for (std::size_t i = 0; i < waypoints_.size(); ++i) {
      std::printf(
        "    [%zu] (%.2f, %.2f) %.1f deg\n", i,
        waypoints_[i].pose.position.x, waypoints_[i].pose.position.y,
        yaw_from_quaternion(
          waypoints_[i].pose.orientation.x, waypoints_[i].pose.orientation.y,
          waypoints_[i].pose.orientation.z, waypoints_[i].pose.orientation.w) * 180.0 / kPi);
    }
    std::fflush(stdout);

    const auto before = pose_;
    follow_start_ = std::chrono::steady_clock::now();
    auto on_feedback = [this, started](const FollowWaypoints::Feedback & feedback) {
        ++follow_feedback_;
        if (feedback.current_waypoint != follow_current_) {
          follow_current_ = feedback.current_waypoint;
          std::printf(
            "  [%6.2fs] waypoint %u of %zu reached, odom (%.2f, %.2f)\n",
            std::chrono::duration<double>(
              std::chrono::steady_clock::now() - started).count(),
            static_cast<unsigned>(follow_current_), waypoints_.size(), pose_.x, pose_.y);
          std::fflush(stdout);
        } else if (options_.verbose) {
          std::printf("  feedback   : current_waypoint %u\n",
            static_cast<unsigned>(follow_current_));
        }
      };

    FollowWaypoints::Result::SharedPtr result;
    rclcpp_action::ResultCode code = rclcpp_action::ResultCode::UNKNOWN;
    if (!send_goal<FollowWaypoints>(follow_client_, options_.follow_action, goal, on_feedback,
      result, code, started))
    {
      return false;
    }
    follow_code_ = code;
    if (result) {
      follow_error_code_ = result->error_code;
      follow_error_msg_ = result->error_msg;
      follow_missed_ = result->missed_waypoints.size();
    }
    follow_travelled_ = distance(before.x, before.y, pose_.x, pose_.y);
    reason_ = result_name(follow_code_);
    if (!follow_error_msg_.empty()) {
      reason_ += " (" + follow_error_msg_ + ")";
    }
    std::printf(
      "  followed   : %zu feedback, last waypoint %u, %s, error %u, %.2f m travelled\n",
      follow_feedback_, static_cast<unsigned>(follow_current_), result_name(follow_code_),
      static_cast<unsigned>(follow_error_code_), follow_travelled_);
    std::fflush(stdout);
    return true;
  }

  // Evenly spaced poses along the path, oriented along its local tangent. The
  // waypoint follower hands each one to `bt_navigator` as a NavigateToPose
  // goal, so these are real navigation targets rather than breadcrumbs.
  std::vector<geometry_msgs::msg::PoseStamped> sample_waypoints(
    const nav_msgs::msg::Path & path) const
  {
    std::vector<geometry_msgs::msg::PoseStamped> out;
    if (path.poses.size() < 2) {
      return out;
    }
    const double total = path_length(path);
    if (total <= 1.0e-3) {
      return out;
    }
    std::vector<double> targets;
    if (options_.spacing > 0.0) {
      for (double s = options_.spacing; s < total - 0.05; s += options_.spacing) {
        targets.push_back(s);
      }
    } else {
      for (int i = 1; i <= options_.count; ++i) {
        targets.push_back(total * i / (options_.count + 1));
      }
    }
    std::size_t leg = 1;
    double covered = 0.0;
    for (double target : targets) {
      while (leg < path.poses.size()) {
        const auto & from = path.poses[leg - 1].pose.position;
        const auto & to = path.poses[leg].pose.position;
        const double leg_length = distance(from.x, from.y, to.x, to.y);
        if (covered + leg_length >= target || leg + 1 == path.poses.size()) {
          const double t = leg_length > 1.0e-9 ?
            std::min(1.0, std::max(0.0, (target - covered) / leg_length)) : 1.0;
          const double x = from.x + (to.x - from.x) * t;
          const double y = from.y + (to.y - from.y) * t;
          out.push_back(make_pose(options_.frame, x, y, std::atan2(to.y - from.y, to.x - from.x),
            node_->now()));
          break;
        }
        covered += leg_length;
        ++leg;
      }
    }
    return out;
  }

  // ------------------------------------------------------------- the action

  // One action, the way every caller of the shim's action client has to send it:
  // no handshake means the response future is the only evidence a server took
  // the goal, and the result future the only evidence it finished.
  template<class ActionT>
  bool send_goal(
    typename rclcpp_action::Client<ActionT>::SharedPtr client,
    const std::string & label,
    const typename ActionT::Goal & goal,
    const std::function<void(const typename ActionT::Feedback &)> & on_feedback,
    typename ActionT::Result::SharedPtr & out_result,
    rclcpp_action::ResultCode & out_code,
    const std::chrono::steady_clock::time_point & started)
  {
    typename rclcpp_action::Client<ActionT>::SendGoalOptions options;
    if (on_feedback) {
      options.feedback_callback =
        [&on_feedback](
        typename rclcpp_action::Client<ActionT>::GoalHandle::SharedPtr,
        std::shared_ptr<const typename ActionT::Feedback> feedback) {
          if (feedback) {
            on_feedback(*feedback);
          }
        };
    }
    const auto sent_at = std::chrono::steady_clock::now();
    auto goal_future = client->async_send_goal(goal, options);
    const auto deadline = sent_at + std::chrono::milliseconds(
      static_cast<int64_t>(options_.timeout_seconds * 1000.0));
    if (!wait_for_future(goal_future, deadline, started)) {
      stage_error_ = label + ": the goal response never arrived (no server on that name?)";
      return false;
    }
    auto handle = goal_future.get();
    if (!handle) {
      stage_error_ = label + ": the server rejected the goal";
      return false;
    }
    const auto accepted_at = std::chrono::steady_clock::now();
    ack_latency_ = std::chrono::duration<double>(accepted_at - sent_at).count();
    std::printf(
      "  accepted   : %s, status %d, ack %.3f s\n", label.c_str(),
      static_cast<int>(handle->get_status()), ack_latency_);
    std::fflush(stdout);

    auto result_future = client->async_get_result(handle);
    if (!wait_for_future(result_future, deadline, started)) {
      stage_error_ = label + ": no result within " +
        std::to_string(static_cast<int>(options_.timeout_seconds)) + "s";
      std::printf("  timeout    : cancelling %s\n", label.c_str());
      client->async_cancel_goal(handle);
      return false;
    }
    const auto wrapped = result_future.get();
    out_code = wrapped.code;
    out_result = wrapped.result;
    stage_elapsed_ = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - accepted_at).count();
    return true;
  }

  template<class FutureT>
  bool wait_for_future(
    const FutureT & future, const std::chrono::steady_clock::time_point & deadline,
    const std::chrono::steady_clock::time_point & started)
  {
    while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      if (std::chrono::steady_clock::now() >= deadline) {
        return false;
      }
      node_->spin_once();
      (void)started;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
  }

  void note_velocity(double linear, double angular)
  {
    max_linear_ = std::max(max_linear_, std::fabs(linear));
    max_angular_ = std::max(max_angular_, std::fabs(angular));
    if (std::fabs(linear) > 1.0e-3 || std::fabs(angular) > 1.0e-3) {
      ++cmd_moving_;
    }
  }

  size_t cmd_total() const {return cmd_count_;}

  static const char * result_name(rclcpp_action::ResultCode code)
  {
    switch (code) {
      case rclcpp_action::ResultCode::SUCCEEDED: return "SUCCEEDED";
      case rclcpp_action::ResultCode::ABORTED: return "ABORTED";
      case rclcpp_action::ResultCode::CANCELED: return "CANCELED";
      case rclcpp_action::ResultCode::UNKNOWN: break;
    }
    return "UNKNOWN";
  }

  int finish(
    bool ok, const std::chrono::steady_clock::time_point & started,
    const std::string & reason, int failure_code)
  {
    const double elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - started).count();
    std::printf("  result     : %s\n", ok ? "SUCCEEDED" : "FAILED");
    std::printf("  reason     : %s\n", reason.c_str());
    std::printf(
      "  odom       : (%.2f, %.2f, %.2f rad) from (%.2f, %.2f), %.2f m travelled\n",
      pose_.x, pose_.y, pose_.yaw, start_pose_.x, start_pose_.y,
      distance(start_pose_.x, start_pose_.y, pose_.x, pose_.y));
    std::printf(
      "  pipeline   : /cmd_vel x%zu (%zu moving, max %.2f m/s, %.2f rad/s), "
      "/plan x%zu, /odom x%zu, %.1f s\n",
      cmd_total(), cmd_moving_, max_linear_, max_angular_, plan_count_,
      odom_count_, elapsed);
    if (!options_.report.empty()) {
      write_report(ok, elapsed, reason);
    }
    std::fflush(stdout);
    rclcpp::shutdown();
    return ok ? 0 : failure_code;
  }

  static std::string json_number(double value)
  {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return buffer;
  }

  void write_report(bool ok, double elapsed, const std::string & reason)
  {
    std::ofstream out(options_.report);
    if (!out) {
      std::printf("  report     : could not open %s\n", options_.report.c_str());
      return;
    }
    out << "{\n";
    out << "  \"result\": \"" << (ok ? "SUCCEEDED" : "FAILED") << "\",\n";
    out << "  \"reason\": \"" << reason << "\",\n";
    out << "  \"elapsed_s\": " << json_number(elapsed) << ",\n";
    out << "  \"goal\": [" << json_number(goal_x_) << ", " << json_number(goal_y_) << "],\n";
    out << "  \"start\": [" << json_number(start_pose_.x) << ", " << json_number(start_pose_.y)
        << ", " << json_number(start_pose_.yaw) << "],\n";
    out << "  \"end\": [" << json_number(pose_.x) << ", " << json_number(pose_.y) << ", "
        << json_number(pose_.yaw) << "],\n";
    out << "  \"plan\": {\"poses\": " << plan_.poses.size()
        << ", \"length_m\": " << json_number(path_length(plan_))
        << ", \"max_turn_deg\": " << json_number(max_turn_degrees(plan_))
        << ", \"error_code\": " << static_cast<unsigned>(plan_error_code_) << "},\n";
    out << "  \"smooth\": {\"poses\": " << smoothed_.poses.size()
        << ", \"input_length_m\": " << json_number(smooth_input_length_)
        << ", \"output_length_m\": " << json_number(smooth_output_length_)
        << ", \"input_max_turn_deg\": " << json_number(smooth_input_turn_)
        << ", \"output_max_turn_deg\": " << json_number(smooth_output_turn_)
        << ", \"was_completed\": " << (smooth_completed_ ? "true" : "false")
        << ", \"error_code\": " << static_cast<unsigned>(smooth_error_code_)
        << ", \"elapsed_s\": " << json_number(stage_elapsed_) << "},\n";
    out << "  \"waypoints\": [";
    for (std::size_t i = 0; i < waypoints_.size(); ++i) {
      out << (i ? ", " : "") << "[" << json_number(waypoints_[i].pose.position.x) << ", "
          << json_number(waypoints_[i].pose.position.y) << "]";
    }
    out << "],\n";
    out << "  \"follow\": {\"feedback\": " << follow_feedback_
        << ", \"last_waypoint\": " << static_cast<unsigned>(follow_current_)
        << ", \"missed_waypoints\": " << follow_missed_
        << ", \"result_code\": " << static_cast<int>(follow_code_)
        << ", \"error_code\": " << static_cast<unsigned>(follow_error_code_)
        << ", \"error_msg\": \"" << follow_error_msg_ << "\""
        << ", \"travelled_m\": " << json_number(follow_travelled_)
        << ", \"elapsed_s\": " << json_number(stage_elapsed_) << "},\n";
    out << "  \"cmd_vel\": {\"messages\": " << cmd_total()
        << ", \"moving\": " << cmd_moving_
        << ", \"max_linear\": " << json_number(max_linear_)
        << ", \"max_angular\": " << json_number(max_angular_) << "}\n";
    out << "}\n";
    std::printf("  report     : %s\n", options_.report.c_str());
  }

  Options options_;
  std::shared_ptr<rclcpp::Node> node_;
  PlanClient::SharedPtr plan_client_;
  SmoothClient::SharedPtr smooth_client_;
  FollowClient::SharedPtr follow_client_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_subscription_;
  rclcpp::Subscription<nav_msgs::msg::OccupancyGrid>::SharedPtr map_subscription_;
  rclcpp::Subscription<nav_msgs::msg::Path>::SharedPtr plan_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_subscription_;

  Pose2D pose_;
  Pose2D start_pose_;
  nav_msgs::msg::OccupancyGrid::ConstSharedPtr grid_;
  nav_msgs::msg::Path plan_;
  nav_msgs::msg::Path smoothed_;
  std::vector<geometry_msgs::msg::PoseStamped> waypoints_;
  double goal_x_ = 0.0;
  double goal_y_ = 0.0;

  size_t odom_count_ = 0;
  size_t map_count_ = 0;
  size_t plan_count_ = 0;
  size_t plan_poses_ = 0;
  size_t cmd_count_ = 0;
  size_t cmd_moving_ = 0;
  double max_linear_ = 0.0;
  double max_angular_ = 0.0;

  size_t follow_feedback_ = 0;
  uint32_t follow_current_ = 0;
  size_t follow_missed_ = 0;
  double follow_travelled_ = 0.0;
  rclcpp_action::ResultCode follow_code_ = rclcpp_action::ResultCode::UNKNOWN;
  uint16_t follow_error_code_ = 0;
  std::string follow_error_msg_;

  rclcpp_action::ResultCode plan_code_ = rclcpp_action::ResultCode::UNKNOWN;
  uint16_t plan_error_code_ = 0;
  rclcpp_action::ResultCode smooth_code_ = rclcpp_action::ResultCode::UNKNOWN;
  uint16_t smooth_error_code_ = 0;
  std::string smooth_error_msg_;
  bool smooth_completed_ = false;
  double smooth_input_length_ = 0.0;
  double smooth_output_length_ = 0.0;
  double smooth_input_turn_ = 0.0;
  double smooth_output_turn_ = 0.0;

  double ack_latency_ = 0.0;
  double stage_elapsed_ = 0.0;
  std::string stack_error_;
  std::string goal_error_;
  std::string stage_error_;
  std::string reason_ = "not started";
  std::chrono::steady_clock::time_point follow_start_;
};

int main(int argc, char ** argv)
{
  Options options;
  const int parsed = parse_args(argc, argv, options);
  if (parsed == 0 && options.help) {
    print_usage();
    return 0;
  }
  if (parsed != 0) {
    print_usage();
    return parsed;
  }
  Waypoints runner(options);
  return runner.run(argc, argv);
}
