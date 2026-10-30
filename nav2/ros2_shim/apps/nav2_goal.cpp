// ---------------------------------------------------------------------------
// nav2_goal.cpp
//
// Sends one `nav2_msgs/action/NavigateToPose` goal at the nav2 stack that runs
// on Windows, and reports what the behavior tree did with it.
//
//   Ubuntu (simulated robot)                      Windows
//   ------------------------                      -------
//   /scan /odom /map  --gRPC--> daemon --shm-->  amcl_bridge_node  (map -> odom)
//                                                planner_server    (global costmap)
//       ^                                        controller_server (local costmap)
//       |  /cmd_vel <--gRPC-- daemon <--shm--          ^        ^
//       |                                         bt_navigator --+
//       +--- the car moves, /odom changes         (this process sends the goal)
//
// The three servers are the ported upstream nodes, pulled to ACTIVE by
// `lifecycle_manager.exe`. This process only speaks the action interface: it
// waits for `bt_navigator` to appear in the shim graph, sends the goal, prints
// the feedback the navigator publishes, and reads the result the behavior tree
// produced. Nothing in the pipeline is shortcut, so a failure here means the
// behavior tree really could not drive the planner and the controller.
//
// Run it with the bridge environment loaded (see bridge/windows/nav2_stack.bat):
//
//   nav2_goal.exe --goal 3.5,-2.0
//   nav2_goal.exe --goal auto --report goal.json
//
// `auto` picks a goal out of the bridged `/map`: the direction with the longest
// clear run away from the robot, capped at --auto-radius and backed off by
// --auto-clearance. The simulator's world is seeded, so its free space is not
// known ahead of time; `auto` is how the demo stays a one-liner anyway.
// ---------------------------------------------------------------------------

#include "geometry_msgs/msg/twist.hpp"
#include "geometry_msgs/msg/twist_stamped.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
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

struct Pose2D
{
  double x = 0.0;
  double y = 0.0;
  double yaw = 0.0;
};

// ------------------------------------------------------------ the shim graph

// "Is the action server there?" is not a question the shim's action client can
// answer: a shared-memory channel has no handshake, so `wait_for_action_server()`
// returns true unconditionally (see rclcpp_action/rclcpp_action.hpp). The process
// registry is the honest answer - every node announces itself there as it is
// constructed, whether it lives in this process or in another one.
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
      return true;  // "/bt_navigator" answers to "bt_navigator"
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
  double yaw_deg = 0.0;
  std::string frame = "map";
  std::string action = "navigate_to_pose";
  std::string behavior_tree;
  double wait_seconds = 60.0;
  double timeout_seconds = 180.0;
  double auto_radius = 3.0;
  double auto_clearance = 0.4;
  double status_period = 1.0;
  std::string report;
  bool verbose = false;
  bool help = false;
};

void print_usage()
{
  std::printf(
    "usage: nav2_goal.exe [--goal X,Y|auto] [options]\n"
    "\n"
    "Sends one NavigateToPose goal at the nav2 stack running on Windows.\n"
    "\n"
    "  --goal X,Y|auto     goal in the map frame (default: auto)\n"
    "  --yaw DEG           heading to finish in (default: 0)\n"
    "  --frame NAME        frame the goal is given in (default: map)\n"
    "  --action NAME       action to send it to (default: navigate_to_pose)\n"
    "  --bt FILE_OR_ID     behavior tree to run instead of the default one\n"
    "  --wait SEC          wait for bt_navigator + /odom + /map (default: 60)\n"
    "  --timeout SEC       give up on the goal after this (default: 180)\n"
    "  --auto-radius M     how far `auto` looks for clear space (default: 3.0)\n"
    "  --auto-clearance M  free radius `auto` insists on (default: 0.4)\n"
    "  --status SEC        status line period, 0 turns them off (default: 1.0)\n"
    "  --report FILE       write a JSON summary\n"
    "  -v                  print every feedback message, not just the status\n"
    "  -h, --help          this text\n");
}

bool parse_double(const std::string & text, double & value)
{
  char * end = nullptr;
  value = std::strtod(text.c_str(), &end);
  return end != text.c_str() && *end == '\0';
}

bool parse_goal(const std::string & text, double & x, double & y)
{
  const size_t comma = text.find(',');
  if (comma == std::string::npos) {
    return false;
  }
  return parse_double(text.substr(0, comma), x) &&
         parse_double(text.substr(comma + 1), y);
}

// 0 = carry on, 2 = bad arguments, and `options.help` means "print the usage and
// exit successfully".
int parse_args(int argc, char ** argv, Options & options)
{
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    auto value = [&](const char * name) -> const char * {
        if (index + 1 >= argc) {
          std::fprintf(stderr, "%s needs a value\n", name);
          return nullptr;
        }
        return argv[++index];
      };

    if (arg == "-h" || arg == "--help") {
      print_usage();
      options.help = true;
      return 0;
    } else if (arg == "-v") {
      options.verbose = true;
    } else if (arg == "--goal") {
      const char * text = value("--goal");
      if (!text) {
        return 2;
      }
      options.goal = text;
      if (options.goal != "auto" &&
        !parse_goal(options.goal, options.goal_x, options.goal_y))
      {
        std::fprintf(stderr, "--goal wants X,Y or auto, got '%s'\n", text);
        return 2;
      }
    } else if (arg == "--yaw") {
      const char * text = value("--yaw");
      if (!text || !parse_double(text, options.yaw_deg)) {
        std::fprintf(stderr, "--yaw wants degrees\n");
        return 2;
      }
    } else if (arg == "--frame") {
      const char * text = value("--frame");
      if (!text) {
        return 2;
      }
      options.frame = text;
    } else if (arg == "--action") {
      const char * text = value("--action");
      if (!text) {
        return 2;
      }
      options.action = text;
    } else if (arg == "--bt") {
      const char * text = value("--bt");
      if (!text) {
        return 2;
      }
      options.behavior_tree = text;
    } else if (arg == "--wait") {
      const char * text = value("--wait");
      if (!text || !parse_double(text, options.wait_seconds)) {
        std::fprintf(stderr, "--wait wants seconds\n");
        return 2;
      }
    } else if (arg == "--timeout") {
      const char * text = value("--timeout");
      if (!text || !parse_double(text, options.timeout_seconds)) {
        std::fprintf(stderr, "--timeout wants seconds\n");
        return 2;
      }
    } else if (arg == "--auto-radius") {
      const char * text = value("--auto-radius");
      if (!text || !parse_double(text, options.auto_radius)) {
        std::fprintf(stderr, "--auto-radius wants metres\n");
        return 2;
      }
    } else if (arg == "--auto-clearance") {
      const char * text = value("--auto-clearance");
      if (!text || !parse_double(text, options.auto_clearance)) {
        std::fprintf(stderr, "--auto-clearance wants metres\n");
        return 2;
      }
    } else if (arg == "--status") {
      const char * text = value("--status");
      if (!text || !parse_double(text, options.status_period)) {
        std::fprintf(stderr, "--status wants seconds\n");
        return 2;
      }
    } else if (arg == "--report") {
      const char * text = value("--report");
      if (!text) {
        return 2;
      }
      options.report = text;
    } else {
      std::fprintf(stderr, "unknown argument '%s'\n", arg.c_str());
      print_usage();
      return 2;
    }
  }
  return 0;
}

// ------------------------------------------------------------------- client

using NavigateToPose = nav2_msgs::action::NavigateToPose;
using ActionClient = rclcpp_action::Client<NavigateToPose>;

class GoalClient
{
public:
  explicit GoalClient(const Options & options)
  : options_(options)
  {
  }

  int run(int argc, char ** argv)
  {
    rclcpp::init(argc, argv);
    node_ = std::make_shared<rclcpp::Node>("nav2_goal");

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
    // `/cmd_vel` is published as Twist or as TwistStamped, depending on nav2's
    // `enable_stamped_cmd_vel` (nav2_util::TwistPublisher). The two do not
    // decode from the same bytes and the shim does not compare types, so this
    // listens for both layouts and reports the one that actually produced
    // messages: a quiet `/cmd_vel x0` here is exactly what a controller writing
    // the other layout looks like from the outside.
    cmd_subscription_ = node_->create_subscription<geometry_msgs::msg::Twist>(
      "/cmd_vel", 50,
      [this](const geometry_msgs::msg::Twist::ConstSharedPtr message) {
        ++cmd_count_;
        note_velocity(message->linear.x, message->angular.z);
      });
    cmd_stamped_subscription_ =
      node_->create_subscription<geometry_msgs::msg::TwistStamped>(
      "/cmd_vel", 50,
      [this](const geometry_msgs::msg::TwistStamped::ConstSharedPtr message) {
        ++cmd_stamped_count_;
        note_velocity(message->twist.linear.x, message->twist.angular.z);
      });

    action_client_ = rclcpp_action::create_client<NavigateToPose>(node_, options_.action);

    std::printf("=== nav2_goal ===\n");
    std::printf("  action     : %s\n", options_.action.c_str());
    std::printf("  goal       : %s\n", options_.goal.c_str());
    std::printf("  frame      : %s\n", options_.frame.c_str());
    if (!options_.behavior_tree.empty()) {
      std::printf("  tree       : %s\n", options_.behavior_tree.c_str());
    }

    const auto started = std::chrono::steady_clock::now();
    if (!wait_for_stack(started)) {
      return finish(false, started, stack_error_, 1);
    }
    std::printf(
      "  stack      : bt_navigator present, /odom x%zu, /map x%zu (%dx%d @ %.3f m)\n",
      odom_count_, map_count_, grid_ ? static_cast<int>(grid_->info.width) : 0,
      grid_ ? static_cast<int>(grid_->info.height) : 0,
      grid_ ? grid_->info.resolution : 0.0);

    if (!choose_goal()) {
      return finish(false, started, goal_error_, 1);
    }
    start_pose_ = pose_;
    std::printf(
      "  robot      : (%.2f, %.2f, %.2f rad), goal (%.2f, %.2f), %.2f m away\n",
      pose_.x, pose_.y, pose_.yaw, goal_x_, goal_y_,
      distance(pose_.x, pose_.y, goal_x_, goal_y_));

    const int sent = send_goal(started);
    if (sent != 0) {
      return sent;  // send_goal() already printed and wrote the report
    }
    return finish(result_code_ == rclcpp_action::ResultCode::SUCCEEDED, started, reason_, 0);
  }

private:
  // -------------------------------------------------------------- readiness

  // Waits until the pieces this client needs are actually there: `bt_navigator`
  // in the graph (so the goal is not published into a channel nobody reads),
  // `/odom` (the report is written from it) and - for `auto` - `/map`.
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
        next_report = now + std::chrono::seconds(5);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  bool stack_ready()
  {
    if (!shim_node_present("bt_navigator")) {
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
    auto add = [&missing](const char * piece) {
        if (!missing.empty()) {
          missing += ", ";
        }
        missing += piece;
      };
    if (!shim_node_present("bt_navigator")) {
      add("bt_navigator in the shim graph");
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
    double best_x = pose_.x;
    double best_y = pose_.y;
    for (int beam = 0; beam < 360; ++beam) {
      const double angle = 2.0 * kPi * beam / 360.0;
      double run = 0.0;
      for (double reach = step; reach <= options_.auto_radius; reach += step) {
        const double x = pose_.x + std::cos(angle) * reach;
        const double y = pose_.y + std::sin(angle) * reach;
        if (!free_with_clearance(x, y)) {
          break;
        }
        run = reach;
      }
      if (run > best_run) {
        best_run = run;
        best_x = pose_.x + std::cos(angle) * run;
        best_y = pose_.y + std::sin(angle) * run;
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
    const double heading = std::atan2(best_y - pose_.y, best_x - pose_.x);
    goal_x_ = pose_.x + std::cos(heading) * reach;
    goal_y_ = pose_.y + std::sin(heading) * reach;
    goal_source_ = "auto";
    return true;
  }

  // -------------------------------------------------------------- the goal

  int send_goal(const std::chrono::steady_clock::time_point & started)
  {
    auto goal = std::make_shared<NavigateToPose::Goal>();
    goal->pose.header.frame_id = options_.frame;
    goal->pose.header.stamp = node_->now();
    goal->pose.pose.position.x = goal_x_;
    goal->pose.pose.position.y = goal_y_;
    const double half_yaw = options_.yaw_deg * kPi / 360.0;
    goal->pose.pose.orientation.z = std::sin(half_yaw);
    goal->pose.pose.orientation.w = std::cos(half_yaw);
    goal->behavior_tree = options_.behavior_tree;

    typename ActionClient::SendGoalOptions send_options;
    send_options.feedback_callback =
      [this, started](typename ActionClient::GoalHandle::SharedPtr,
        std::shared_ptr<const NavigateToPose::Feedback> feedback) {
        if (!feedback) {
          return;
        }
        ++feedback_count_;
        last_distance_remaining_ = feedback->distance_remaining;
        last_recoveries_ = feedback->number_of_recoveries;
        if (options_.verbose || options_.status_period > 0.0) {
          print_status("feedback", started, feedback->distance_remaining);
        }
      };

    std::printf("  sending    : NavigateToPose x=%.2f y=%.2f yaw=%.1f deg\n",
      goal_x_, goal_y_, options_.yaw_deg);
    // Timed on both sides of the exchange: the shim delivers a goal over shared
    // memory with no handshake, so "how long until the server said yes" is the
    // only latency figure there is - and the behavior tree's own action clients
    // live on the same mechanism, with a `server_timeout` budget around it.
    const auto sent_at = std::chrono::steady_clock::now();
    auto goal_future = action_client_->async_send_goal(*goal, send_options);
    const auto deadline = std::chrono::steady_clock::now() +
      std::chrono::milliseconds(static_cast<int64_t>(options_.timeout_seconds * 1000.0));

    if (!wait_for_future(goal_future, deadline, started)) {
      result_code_ = rclcpp_action::ResultCode::UNKNOWN;
      reason_ = "the goal response never arrived (no server on " + options_.action + "?)";
      return finish(false, started, reason_, 1);
    }
    auto handle = goal_future.get();
    if (!handle) {
      result_code_ = rclcpp_action::ResultCode::ABORTED;
      reason_ = "the goal was rejected";
      std::printf("  rejected   : the navigator refused the goal\n");
      return finish(false, started, reason_, 3);
    }
    const auto accepted_at = std::chrono::steady_clock::now();
    ack_latency_ = std::chrono::duration<double>(accepted_at - sent_at).count();
    std::printf("  accepted   : status %d after %.3f s\n",
      static_cast<int>(handle->get_status()), ack_latency_);

    auto result_future = action_client_->async_get_result(handle);
    if (!wait_for_future(result_future, deadline, started)) {
      result_code_ = rclcpp_action::ResultCode::UNKNOWN;
      reason_ = "the goal did not finish within " +
        std::to_string(static_cast<int>(options_.timeout_seconds)) + "s";
      std::printf("  timeout    : cancelling\n");
      action_client_->async_cancel_goal(handle);
      return finish(false, started, reason_, 1);
    }
    const auto wrapped = result_future.get();
    result_latency_ = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - accepted_at).count();
    result_code_ = wrapped.code;
    if (wrapped.result) {
      error_code_ = wrapped.result->error_code;
      error_msg_ = wrapped.result->error_msg;
    }
    reason_ = result_name(result_code_);
    return 0;
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
      const auto now = std::chrono::steady_clock::now();
      if (options_.status_period > 0.0 &&
        now - last_status_ >= std::chrono::milliseconds(
          static_cast<int64_t>(options_.status_period * 1000.0)))
      {
        print_status("status", started, last_distance_remaining_);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
  }

  void note_velocity(double linear_x, double angular_z)
  {
    last_linear_ = linear_x;
    last_angular_ = angular_z;
    max_linear_ = std::max(max_linear_, std::abs(linear_x));
    if (std::abs(linear_x) > 1e-3 || std::abs(angular_z) > 1e-3) {
      ++cmd_moving_;
    }
  }

  size_t cmd_total() const {return cmd_count_ + cmd_stamped_count_;}

  // Which layout of `/cmd_vel` the stack is publishing, the way nav2's
  // `enable_stamped_cmd_vel` left it.
  const char * cmd_vel_type() const
  {
    if (cmd_stamped_count_ > 0) {
      return "TwistStamped";
    }
    return cmd_count_ > 0 ? "Twist" : "(none)";
  }

  // One line per second while the behavior tree works: where the car is, how far
  // it still has to go, and - the part that proves the pipeline is real - how
  // many /cmd_vel and /plan messages the servers produced.
  void print_status(
    const char * tag, const std::chrono::steady_clock::time_point & started,
    float distance_remaining)
  {
    const auto now = std::chrono::steady_clock::now();
    last_status_ = now;
    const double elapsed = std::chrono::duration<double>(now - started).count();
    std::printf(
      "  [%6.2fs] %-8s odom (%.2f, %.2f)  remaining %.2f m  %zu feedback  "
      "/cmd_vel x%zu %s (max %.2f)  /plan x%zu (%zu poses)\n",
      elapsed, tag, pose_.x, pose_.y, distance_remaining, feedback_count_,
      cmd_total(), cmd_vel_type(), max_linear_, plan_count_, plan_poses_);
  }

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
    const double travelled = distance(start_pose_.x, start_pose_.y, pose_.x, pose_.y);
    const double remaining = distance(pose_.x, pose_.y, goal_x_, goal_y_);

    std::printf("  result     : %s\n", ok ? "SUCCEEDED" : "FAILED");
    std::printf("  reason     : %s\n", reason.c_str());
    if (error_code_ != 0 || !error_msg_.empty()) {
      std::printf("  error      : code %u, msg '%s'\n",
        static_cast<unsigned>(error_code_), error_msg_.c_str());
    }
    std::printf("  odom       : (%.2f, %.2f, %.2f rad) from (%.2f, %.2f)\n",
      pose_.x, pose_.y, pose_.yaw, start_pose_.x, start_pose_.y);
    std::printf("  travelled  : %.2f m, %.2f m left to the goal\n", travelled, remaining);
    std::printf("  pipeline   : /cmd_vel x%zu %s (%zu moving, max %.2f m/s), "
      "/plan x%zu (last %zu poses), /odom x%zu, %.1f s\n",
      cmd_total(), cmd_vel_type(), cmd_moving_, max_linear_, plan_count_, plan_poses_,
      odom_count_, elapsed);

    if (!options_.report.empty()) {
      write_report(ok, elapsed, reason, travelled, remaining);
    }
    std::fflush(stdout);
    rclcpp::shutdown();
    return ok ? 0 : failure_code;
  }

  void write_report(
    bool ok, double elapsed, const std::string & reason, double travelled,
    double remaining)
  {
    std::ofstream out(options_.report);
    if (!out) {
      std::fprintf(stderr, "warning: could not write the report to %s\n",
        options_.report.c_str());
      return;
    }
    out << "{\n";
    out << "  \"succeeded\": " << (ok ? "true" : "false") << ",\n";
    out << "  \"reason\": \"" << reason << "\",\n";
    out << "  \"action\": \"" << options_.action << "\",\n";
    out << "  \"goal\": [" << goal_x_ << ", " << goal_y_ << ", " << options_.yaw_deg << "],\n";
    out << "  \"goal_source\": \"" << options_.goal << "\",\n";
    out << "  \"frame\": \"" << options_.frame << "\",\n";
    out << "  \"result\": \"" << result_name(result_code_) << "\",\n";
    out << "  \"error_code\": " << static_cast<unsigned>(error_code_) << ",\n";
    out << "  \"error_msg\": \"" << error_msg_ << "\",\n";
    out << "  \"elapsed_s\": " << elapsed << ",\n";
    out << "  \"start_pose\": [" << start_pose_.x << ", " << start_pose_.y << "],\n";
    out << "  \"final_pose\": [" << pose_.x << ", " << pose_.y << ", " << pose_.yaw << "],\n";
    out << "  \"travelled_m\": " << travelled << ",\n";
    out << "  \"remaining_m\": " << remaining << ",\n";
    out << "  \"feedback_messages\": " << feedback_count_ << ",\n";
    out << "  \"number_of_recoveries\": " << last_recoveries_ << ",\n";
    out << "  \"last_distance_remaining_m\": " << last_distance_remaining_ << ",\n";
    out << "  \"max_linear_velocity\": " << max_linear_ << ",\n";
    out << "  \"ack_latency_s\": " << ack_latency_ << ",\n";
    out << "  \"result_latency_s\": " << result_latency_ << ",\n";
    out << "  \"messages\": {\"cmd_vel\": " << cmd_total()
        << ", \"cmd_vel_type\": \"" << cmd_vel_type() << "\""
        << ", \"cmd_vel_moving\": " << cmd_moving_
        << ", \"plan\": " << plan_count_ << ", \"plan_poses\": " << plan_poses_
        << ", \"odom\": " << odom_count_ << "}\n";
    out << "}\n";
    std::printf("  report     : %s\n", options_.report.c_str());
  }

  Options options_;

  rclcpp::Node::SharedPtr node_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_subscription_;
  rclcpp::Subscription<nav_msgs::msg::OccupancyGrid>::SharedPtr map_subscription_;
  rclcpp::Subscription<nav_msgs::msg::Path>::SharedPtr plan_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr
    cmd_stamped_subscription_;
  ActionClient::SharedPtr action_client_;

  nav_msgs::msg::OccupancyGrid::ConstSharedPtr grid_;
  Pose2D pose_;
  Pose2D start_pose_;
  double goal_x_ = 0.0;
  double goal_y_ = 0.0;
  std::string goal_source_;
  std::string goal_error_;
  std::string stack_error_;
  std::string reason_ = "not started";

  size_t odom_count_ = 0;
  size_t map_count_ = 0;
  size_t plan_count_ = 0;
  size_t plan_poses_ = 0;
  size_t cmd_count_ = 0;
  size_t cmd_stamped_count_ = 0;
  size_t cmd_moving_ = 0;
  size_t feedback_count_ = 0;
  double max_linear_ = 0.0;
  double ack_latency_ = 0.0;
  double result_latency_ = 0.0;
  double last_linear_ = 0.0;
  double last_angular_ = 0.0;
  float last_distance_remaining_ = 0.0F;
  int16_t last_recoveries_ = 0;

  rclcpp_action::ResultCode result_code_ = rclcpp_action::ResultCode::UNKNOWN;
  uint16_t error_code_ = 0;
  std::string error_msg_;

  std::chrono::steady_clock::time_point last_status_ = std::chrono::steady_clock::now();
};

}  // namespace

int main(int argc, char ** argv)
{
  // Keep our own lines in order with the shim's stderr logging.
  std::setvbuf(stdout, nullptr, _IOLBF, 0);

  Options options;
  const int parsed = parse_args(argc, argv, options);
  if (options.help) {
    return 0;
  }
  if (parsed != 0) {
    return parsed;
  }
  GoalClient client(options);
  return client.run(argc, argv);
}
