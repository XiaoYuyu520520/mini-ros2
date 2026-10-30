// ---------------------------------------------------------------------------
// nav2_bridge_demo.cpp
//
// Closed loop demonstration of the whole project:
//
//   Ubuntu (simulated robot)                       Windows
//   ------------------------                       -------
//   /scan /odom /map  ---gRPC--->  ros2_bridge_daemon.exe  ---shm--->
//                                                       |
//                                     +----------------------------------+
//                                     |  this process (shim + nav2)      |
//                                     |   occupancy grid <- /map,/scan   |
//                                     |   nav2_navfn_planner             |
//                                     |   pure pursuit controller        |
//                                     +----------------------------------+
//                                                       |
//   simulated car moves  <--gRPC--  ros2_bridge_daemon.exe  <--shm--  /cmd_vel
//
// Nothing here is stubbed. The costmap, the laser integration and the planner
// are the ported navigation2 code, the topics travel through the shim shared
// memory segments and across the wire through the gRPC bridge, and the result
// is verified against the odometry that comes back from the robot.
//
// Run it with the bridge environment loaded (see bridge/windows/nav2_demo.bat):
//
//   nav2_bridge_demo.exe --goal 5,-4
//   nav2_bridge_demo.exe --goal auto --map-source laser --report run.json
// ---------------------------------------------------------------------------

#include "geometry_msgs/msg/twist.hpp"
#include "nav2_navfn_planner/navfn.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "nav_msgs/msg/path.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

// ------------------------------------------------------------------ helpers

// nav2_costmap_2d/cost_values.hpp
constexpr uint8_t COST_FREE = 0;
constexpr uint8_t COST_INSCRIBED = 253;
constexpr uint8_t COST_LETHAL = 254;
constexpr uint8_t COST_UNKNOWN = 255;

constexpr double PI = 3.14159265358979323846;

double normalize_angle(double angle)
{
  while (angle > PI) {
    angle -= 2.0 * PI;
  }
  while (angle < -PI) {
    angle += 2.0 * PI;
  }
  return angle;
}

double yaw_from_quaternion(double x, double y, double z, double w)
{
  const double sin_yaw = 2.0 * (w * z + x * y);
  const double cos_yaw = 1.0 - 2.0 * (y * y + z * z);
  return std::atan2(sin_yaw, cos_yaw);
}

double distance(double x0, double y0, double x1, double y1)
{
  return std::hypot(x1 - x0, y1 - y0);
}

struct Pose2D
{
  double x = 0.0;
  double y = 0.0;
  double yaw = 0.0;
};

void sleep_ms(double milliseconds)
{
  std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(milliseconds));
}// ------------------------------------------------------------------ options

struct Options
{
  std::string goal = "auto";         // "auto" or "X,Y"
  std::string map_source = "union";  // map | laser | union
  std::string report;                // optional JSON report path

  double wait_topics = 30.0;         // seconds to wait for /map, /odom and /scan
  double timeout = 180.0;            // give up driving after this many seconds
  double tolerance = 0.30;           // goal reached when closer than this [m]

  double max_linear = 0.6;           // [m/s]
  double max_angular = 1.8;          // [rad/s]
  double lookahead = 0.7;            // pure pursuit lookahead [m]
  double control_rate = 10.0;        // [Hz]
  double replan_period = 3.0;        // seconds between global replans

  double inscribed_radius = 0.30;    // [m]
  double inflation_radius = 0.80;    // [m]
  double cost_scaling = 3.0;
  double footprint_radius = 0.35;    // cells this close to the robot are cleared

  bool plan_only = false;            // plan and publish /plan, never drive
  bool show_topics = true;
  bool verbose = false;
};

void print_usage()
{
  std::printf(
    "nav2_bridge_demo - plan and drive nav2 on Windows for a robot bridged over gRPC\n"
    "\n"
    "  --goal X,Y | auto     target in the map frame (default: auto, the furthest\n"
    "                        reachable free cell)\n"
    "  --map-source MODE     map | laser | union (default union): use the /map that\n"
    "                        the peer publishes, an occupancy grid built from /scan,\n"
    "                        or both\n"
    "  --wait-topics SEC     how long to wait for /map, /odom and /scan (default 30)\n"
    "  --timeout SEC         abort driving after this long (default 180)\n"
    "  --tolerance M         goal tolerance (default 0.30)\n"
    "  --max-linear MPS      speed limit (default 0.6)\n"
    "  --max-angular RPS     yaw rate limit (default 1.8)\n"
    "  --lookahead M         pure pursuit lookahead (default 0.7)\n"
    "  --rate HZ             control loop rate (default 10)\n"
    "  --replan SEC          global replan period (default 3)\n"
    "  --inscribed M         inscribed radius used for inflation (default 0.30)\n"
    "  --inflation M         inflation radius (default 0.80)\n"
    "  --cost-scaling K      inflation cost scaling (default 3.0)\n"
    "  --footprint M         radius around the robot cleared before planning (default 0.35)\n"
    "  --plan-only           publish /plan but do not publish /cmd_vel\n"
    "  --report FILE         write a JSON summary of the run\n"
    "  --no-topics           do not print the shim graph at startup\n"
    "  -v, --verbose         print every control cycle\n"
    "  -h, --help            this text\n");
}

bool parse_double(const std::string & text, double & value)
{
  try {
    std::size_t used = 0;
    const double parsed = std::stod(text, &used);
    if (used != text.size()) {
      return false;
    }
    value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_args(int argc, char ** argv, Options & options)
{
  const auto need_value = [&](int & index, const char * flag, std::string & out) {
      if (index + 1 >= argc) {
        std::fprintf(stderr, "error: %s needs a value\n", flag);
        return false;
      }
      out = argv[++index];
      return true;
    };
  const auto need_double = [&](int & index, const char * flag, double & out) {
      std::string text;
      if (!need_value(index, flag, text)) {
        return false;
      }
      if (!parse_double(text, out)) {
        std::fprintf(stderr, "error: %s expects a number, got '%s'\n", flag, text.c_str());
        return false;
      }
      return true;
    };

  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "-h" || arg == "--help") {
      print_usage();
      return false;
    } else if (arg == "--goal") {
      if (!need_value(index, "--goal", options.goal)) {return false;}
    } else if (arg == "--map-source") {
      if (!need_value(index, "--map-source", options.map_source)) {return false;}
    } else if (arg == "--wait-topics") {
      if (!need_double(index, "--wait-topics", options.wait_topics)) {return false;}
    } else if (arg == "--timeout") {
      if (!need_double(index, "--timeout", options.timeout)) {return false;}
    } else if (arg == "--tolerance") {
      if (!need_double(index, "--tolerance", options.tolerance)) {return false;}
    } else if (arg == "--max-linear") {
      if (!need_double(index, "--max-linear", options.max_linear)) {return false;}
    } else if (arg == "--max-angular") {
      if (!need_double(index, "--max-angular", options.max_angular)) {return false;}
    } else if (arg == "--lookahead") {
      if (!need_double(index, "--lookahead", options.lookahead)) {return false;}
    } else if (arg == "--rate") {
      if (!need_double(index, "--rate", options.control_rate)) {return false;}
    } else if (arg == "--replan") {
      if (!need_double(index, "--replan", options.replan_period)) {return false;}
    } else if (arg == "--inscribed") {
      if (!need_double(index, "--inscribed", options.inscribed_radius)) {return false;}
    } else if (arg == "--inflation") {
      if (!need_double(index, "--inflation", options.inflation_radius)) {return false;}
    } else if (arg == "--cost-scaling") {
      if (!need_double(index, "--cost-scaling", options.cost_scaling)) {return false;}
    } else if (arg == "--footprint") {
      if (!need_double(index, "--footprint", options.footprint_radius)) {return false;}
    } else if (arg == "--report") {
      if (!need_value(index, "--report", options.report)) {return false;}
    } else if (arg == "--plan-only") {
      options.plan_only = true;
    } else if (arg == "--no-topics") {
      options.show_topics = false;
    } else if (arg == "-v" || arg == "--verbose") {
      options.verbose = true;
    } else {
      std::fprintf(stderr, "error: unknown argument '%s'\n", arg.c_str());
      print_usage();
      return false;
    }
  }

  if (options.map_source != "map" && options.map_source != "laser" &&
    options.map_source != "union")
  {
    std::fprintf(stderr, "error: --map-source must be map, laser or union\n");
    return false;
  }
  if (options.control_rate <= 0.0 || options.timeout <= 0.0) {
    std::fprintf(stderr, "error: --rate and --timeout have to be positive\n");
    return false;
  }
  return true;
}// ------------------------------------------------------------------ costmap

// A nav2_costmap_2d style grid: row major, x fastest, cells in ROS cost
// encoding, origin at the lower left corner of the map.
struct Costmap
{
  int width = 0;
  int height = 0;
  double resolution = 0.05;
  double origin_x = 0.0;
  double origin_y = 0.0;
  std::vector<uint8_t> cost;

  size_t index(int mx, int my) const
  {
    return static_cast<size_t>(my) * static_cast<size_t>(width) + static_cast<size_t>(mx);
  }

  bool contains(int mx, int my) const
  {
    return mx >= 0 && my >= 0 && mx < width && my < height;
  }

  uint8_t at(int mx, int my) const
  {
    return contains(mx, my) ? cost[index(mx, my)] : COST_LETHAL;
  }

  bool world_to_map(double wx, double wy, int & mx, int & my) const
  {
    mx = static_cast<int>(std::floor((wx - origin_x) / resolution));
    my = static_cast<int>(std::floor((wy - origin_y) / resolution));
    return contains(mx, my);
  }

  double cell_center_x(int mx) const {return origin_x + (mx + 0.5) * resolution;}
  double cell_center_y(int my) const {return origin_y + (my + 0.5) * resolution;}

  size_t count(uint8_t value) const
  {
    return static_cast<size_t>(std::count(cost.begin(), cost.end(), value));
  }
};

// Occupancy grid built by ray casting the lidar: -1 unknown, 0 free, 100 occupied.
struct LaserOverlay
{
  int width = 0;
  int height = 0;
  double resolution = 0.05;
  double origin_x = 0.0;
  double origin_y = 0.0;
  std::vector<int8_t> cells;

  size_t marked_free = 0;
  size_t marked_occupied = 0;
  size_t scans = 0;
  size_t beams = 0;
  double closest_return = std::numeric_limits<double>::infinity();

  void reset(const Costmap & costmap)
  {
    width = costmap.width;
    height = costmap.height;
    resolution = costmap.resolution;
    origin_x = costmap.origin_x;
    origin_y = costmap.origin_y;
    cells.assign(static_cast<size_t>(width) * height, static_cast<int8_t>(-1));
    marked_free = 0;
    marked_occupied = 0;
    scans = 0;
    beams = 0;
    closest_return = std::numeric_limits<double>::infinity();
  }

  void set(int mx, int my, int8_t value)
  {
    if (mx < 0 || my < 0 || mx >= width || my >= height) {
      return;
    }
    int8_t & cell = cells[static_cast<size_t>(my) * width + mx];
    if (value == 0 && cell == 100) {
      return;  // a free space ray never erases an obstacle in this demo
    }
    cell = value;
  }

  // Walks every beam in half cell steps: the cells on the way are marked free
  // and the cell that the beam hit is marked occupied. A beam that ran into
  // range_max marks free space only.
  void integrate(const sensor_msgs::msg::LaserScan & scan, const Pose2D & pose)
  {
    const size_t count = scan.ranges.size();
    if (count == 0 || scan.angle_increment == 0.0f) {
      return;
    }
    ++scans;
    beams = count;
    const double step = std::max(1e-3, resolution * 0.5);
    for (size_t beam = 0; beam < count; ++beam) {
      const double range = scan.ranges[beam];
      if (!std::isfinite(range) || range < scan.range_min) {
        continue;
      }
      const double clipped = std::min<double>(range, scan.range_max);
      const bool hit = range < scan.range_max;
      closest_return = std::min(closest_return, clipped);
      const double angle = pose.yaw + scan.angle_min +
        static_cast<double>(beam) * scan.angle_increment;
      const double dx = std::cos(angle);
      const double dy = std::sin(angle);
      for (double travelled = scan.range_min; travelled < clipped - resolution; travelled += step) {
        set(
          static_cast<int>(std::floor((pose.x + dx * travelled - origin_x) / resolution)),
          static_cast<int>(std::floor((pose.y + dy * travelled - origin_y) / resolution)),
          0);
        ++marked_free;
      }
      if (hit) {
        set(
          static_cast<int>(std::floor((pose.x + dx * clipped - origin_x) / resolution)),
          static_cast<int>(std::floor((pose.y + dy * clipped - origin_y) / resolution)),
          100);
        ++marked_occupied;
      }
    }
  }
};

// -------------------------------------------------------------- costmap ops

bool build_base_costmap(
  const nav_msgs::msg::OccupancyGrid & grid, Costmap & costmap, std::string & error)
{
  const auto & info = grid.info;
  if (info.width == 0 || info.height == 0 || info.resolution <= 0.0f) {
    error = "the occupancy grid has no geometry";
    return false;
  }
  if (grid.data.size() != static_cast<size_t>(info.width) * info.height) {
    error = "the occupancy grid data does not match width * height";
    return false;
  }
  costmap.width = static_cast<int>(info.width);
  costmap.height = static_cast<int>(info.height);
  costmap.resolution = info.resolution;
  costmap.origin_x = info.origin.position.x;
  costmap.origin_y = info.origin.position.y;
  costmap.cost.assign(grid.data.size(), COST_FREE);
  for (size_t i = 0; i < grid.data.size(); ++i) {
    const int8_t value = grid.data[i];
    if (value < 0) {
      costmap.cost[i] = COST_UNKNOWN;
    } else if (value >= 50) {
      costmap.cost[i] = COST_LETHAL;
    } else {
      costmap.cost[i] = COST_FREE;
    }
  }
  return true;
}

void merge_laser_overlay(const LaserOverlay & overlay, Costmap & costmap)
{
  for (size_t i = 0; i < costmap.cost.size(); ++i) {
    const int8_t cell = overlay.cells[i];
    if (cell == 100) {
      costmap.cost[i] = COST_LETHAL;
    } else if (cell == 0 && costmap.cost[i] == COST_UNKNOWN) {
      costmap.cost[i] = COST_FREE;
    }
  }
}// 3-4 chamfer distance transform followed by the nav2 inflation model
// (InflationLayer::computeCost): inside the inscribed radius a cell becomes
// INSCRIBED, further out the cost decays exponentially towards free space.
void inflate_costmap(
  Costmap & costmap, double inscribed_radius, double inflation_radius, double cost_scaling)
{
  const int width = costmap.width;
  const int height = costmap.height;
  const int32_t INF = std::numeric_limits<int32_t>::max() / 4;
  std::vector<int32_t> distance(static_cast<size_t>(width) * height, INF);
  for (size_t i = 0; i < distance.size(); ++i) {
    if (costmap.cost[i] == COST_LETHAL) {
      distance[i] = 0;
    }
  }

  const int32_t ORTHO = 3;
  const int32_t DIAG = 4;
  for (int my = 0; my < height; ++my) {
    for (int mx = 0; mx < width; ++mx) {
      const size_t i = static_cast<size_t>(my) * width + mx;
      int32_t best = distance[i];
      if (mx > 0) {best = std::min(best, distance[i - 1] + ORTHO);}
      if (my > 0) {best = std::min(best, distance[i - width] + ORTHO);}
      if (mx > 0 && my > 0) {best = std::min(best, distance[i - width - 1] + DIAG);}
      if (mx + 1 < width && my > 0) {best = std::min(best, distance[i - width + 1] + DIAG);}
      distance[i] = best;
    }
  }
  for (int my = height - 1; my >= 0; --my) {
    for (int mx = width - 1; mx >= 0; --mx) {
      const size_t i = static_cast<size_t>(my) * width + mx;
      int32_t best = distance[i];
      if (mx + 1 < width) {best = std::min(best, distance[i + 1] + ORTHO);}
      if (my + 1 < height) {best = std::min(best, distance[i + width] + ORTHO);}
      if (mx + 1 < width && my + 1 < height) {
        best = std::min(best, distance[i + width + 1] + DIAG);
      }
      if (mx > 0 && my + 1 < height) {
        best = std::min(best, distance[i + width - 1] + DIAG);
      }
      distance[i] = best;
    }
  }

  for (size_t i = 0; i < distance.size(); ++i) {
    // Only inflate into known free space: obstacles keep their lethal value and
    // unknown cells stay unknown so the planner may still cross them.
    if (costmap.cost[i] != COST_FREE || distance[i] >= INF) {
      continue;
    }
    const double metres = distance[i] * (costmap.resolution / 3.0);
    if (metres <= inscribed_radius) {
      costmap.cost[i] = COST_INSCRIBED;
    } else if (metres <= inflation_radius) {
      const double factor = cost_scaling * (metres - inscribed_radius);
      const double value = (COST_INSCRIBED - 1) * std::exp(-factor);
      costmap.cost[i] = static_cast<uint8_t>(std::max(1.0, std::round(value)));
    }
  }
}

// nav2 NavfnPlanner::clearRobotCell: the robot is never inside an obstacle, so
// the cells it covers are forced to free space before planning.
void clear_robot_footprint(Costmap & costmap, const Pose2D & pose, double radius)
{
  int mx = 0;
  int my = 0;
  if (!costmap.world_to_map(pose.x, pose.y, mx, my)) {
    return;
  }
  const int reach = static_cast<int>(std::ceil(radius / costmap.resolution));
  for (int dy = -reach; dy <= reach; ++dy) {
    for (int dx = -reach; dx <= reach; ++dx) {
      if (std::hypot(dx, dy) * costmap.resolution > radius) {
        continue;
      }
      if (costmap.contains(mx + dx, my + dy)) {
        costmap.cost[costmap.index(mx + dx, my + dy)] = COST_FREE;
      }
    }
  }
}

// ------------------------------------------------------------------- planner

struct Plan
{
  std::vector<double> xs;
  std::vector<double> ys;
  double length = 0.0;

  bool empty() const {return xs.size() < 2;}
  size_t size() const {return xs.size();}
};

// The nav2_navfn_planner::NavfnPlanner call sequence: grow the navigation
// function out of the goal, then trace the path back from the robot.
bool plan_path(
  const Costmap & costmap, const Pose2D & start, double goal_x, double goal_y,
  Plan & plan, std::string & error)
{
  plan.xs.clear();
  plan.ys.clear();
  plan.length = 0.0;

  int start_mx = 0;
  int start_my = 0;
  int goal_mx = 0;
  int goal_my = 0;
  if (!costmap.world_to_map(start.x, start.y, start_mx, start_my)) {
    error = "the robot pose is outside the map";
    return false;
  }
  if (!costmap.world_to_map(goal_x, goal_y, goal_mx, goal_my)) {
    error = "the goal is outside the map";
    return false;
  }
  if (costmap.at(goal_mx, goal_my) == COST_LETHAL) {
    error = "the goal is on a lethal obstacle";
    return false;
  }
  if (costmap.at(start_mx, start_my) == COST_LETHAL) {
    error = "the robot is on a lethal obstacle";
    return false;
  }

  nav2_navfn_planner::NavFn planner(costmap.width, costmap.height);
  planner.setCostmap(costmap.cost.data(), true, true);
  int start_cell[2] = {start_mx, start_my};
  int goal_cell[2] = {goal_mx, goal_my};
  planner.setStart(start_cell);
  planner.setGoal(goal_cell);

  // Propagates the potential field out of the goal and reports whether the
  // robot cell became reachable.
  if (!planner.calcNavFnDijkstra([]() {return false;}, true)) {
    error = "no path: the goal is not reachable from the robot";
    return false;
  }
  const int max_cycles =
    std::max(costmap.width, costmap.height) * 4;  // nav2: max_dimension * max_cycles_factor
  if (planner.calcPath(max_cycles) <= 0) {
    error = "no path could be extracted from the potential";
    return false;
  }
  const int length = planner.getPathLen();
  const float * path_x = planner.getPathX();
  const float * path_y = planner.getPathY();
  if (length <= 0) {
    error = "the planner returned an empty path";
    return false;
  }

  std::vector<double> xs;
  std::vector<double> ys;
  xs.reserve(static_cast<size_t>(length) + 2);
  ys.reserve(static_cast<size_t>(length) + 2);
  for (int i = 0; i < length; ++i) {
    xs.push_back(costmap.cell_center_x(static_cast<int>(path_x[i])));
    ys.push_back(costmap.cell_center_y(static_cast<int>(path_y[i])));
    if (xs.size() > 1 && distance(xs[xs.size() - 2], ys[ys.size() - 2], xs.back(), ys.back()) < 1e-6) {
      xs.pop_back();  // drop consecutive duplicates, they add no arc length
      ys.pop_back();
    }
  }
  // Append the requested goal so the controller aims at the exact point.
  if (xs.empty() || distance(xs.back(), ys.back(), goal_x, goal_y) > 1e-6) {
    xs.push_back(goal_x);
    ys.push_back(goal_y);
  }
  plan.xs.swap(xs);
  plan.ys.swap(ys);

  for (size_t i = 1; i < plan.size(); ++i) {
    plan.length += distance(plan.xs[i - 1], plan.ys[i - 1], plan.xs[i], plan.ys[i]);
  }
  if (plan.empty()) {
    error = "the planner returned a path with fewer than two points";
    return false;
  }
  return true;
}

nav_msgs::msg::Path to_path_message(const Plan & plan, const std::string & frame_id)
{
  nav_msgs::msg::Path message;
  message.header.frame_id = frame_id;
  for (size_t i = 0; i < plan.size(); ++i) {
    geometry_msgs::msg::PoseStamped pose;
    pose.header.frame_id = frame_id;
    pose.pose.position.x = plan.xs[i];
    pose.pose.position.y = plan.ys[i];
    pose.pose.orientation.w = 1.0;
    message.poses.push_back(pose);
  }
  return message;
}

}  // namespace// ---------------------------------------------------------------------- demo

namespace
{

// Drives the whole pipeline: subscribe to the bridged topics, build a costmap,
// plan with nav2's NavFn, follow the plan and verify the result over /odom.
class Nav2Demo
{
public:
  explicit Nav2Demo(const Options & options)
  : options_(options)
  {
  }

  int run(int argc, char ** argv)
  {
    rclcpp::init(argc, argv);
    node_ = std::make_shared<rclcpp::Node>("nav2_bridge_demo");

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
    scan_subscription_ = node_->create_subscription<sensor_msgs::msg::LaserScan>(
      "/scan", 10,
      [this](const sensor_msgs::msg::LaserScan::ConstSharedPtr message) {
        scan_ = message;
        ++scan_count_;
      });
    odom_subscription_ = node_->create_subscription<nav_msgs::msg::Odometry>(
      "/odom", 10,
      [this](const nav_msgs::msg::Odometry::ConstSharedPtr message) {
        const auto & position = message->pose.pose.position;
        const auto & orientation = message->pose.pose.orientation;
        pose_.x = position.x;
        pose_.y = position.y;
        pose_.yaw = yaw_from_quaternion(
          orientation.x, orientation.y, orientation.z, orientation.w);
        ++odom_count_;
      });
    cmd_publisher_ = node_->create_publisher<geometry_msgs::msg::Twist>("/cmd_vel", 10);
    plan_publisher_ = node_->create_publisher<nav_msgs::msg::Path>("/plan", 10);

    std::printf("=== nav2_bridge_demo ===\n");
    std::printf("  map source : %s\n", options_.map_source.c_str());
    std::printf("  goal       : %s\n", options_.goal.c_str());
    if (options_.show_topics) {
      print_topics();
    }

    const auto started = std::chrono::steady_clock::now();
    if (!wait_for_topics()) {
      return finish(false, started, "the bridged topics never arrived");
    }
    std::printf(
      "  received   : /map x%zu (%dx%d @ %.3f m), /scan x%zu (%zu beams), /odom x%zu\n",
      map_count_, grid_ ? static_cast<int>(grid_->info.width) : 0,
      grid_ ? static_cast<int>(grid_->info.height) : 0,
      grid_ ? grid_->info.resolution : 0.0,
      scan_count_, scan_ ? scan_->ranges.size() : 0, odom_count_);
    std::printf("  robot      : (%.2f, %.2f, %.2f rad) from the latest /odom\n",
      pose_.x, pose_.y, pose_.yaw);
    start_pose_ = pose_;

    if (!build_costmap()) {
      return finish(false, started, costmap_error_);
    }
    std::printf(
      "  costmap    : %dx%d, %zu lethal, %zu inscribed, %zu unknown cells\n",
      costmap_.width, costmap_.height, costmap_.count(COST_LETHAL),
      costmap_.count(COST_INSCRIBED), costmap_.count(COST_UNKNOWN));
    if (laser_enabled() && laser_.scans > 0) {
      std::printf(
        "  laser      : %zu scan(s), %zu beams, %zu free marks, %zu occupied marks, "
        "closest return %.2f m\n",
        laser_.scans, laser_.beams, laser_.marked_free, laser_.marked_occupied,
        std::isfinite(laser_.closest_return) ? laser_.closest_return : -1.0);
    }

    double goal_x = 0.0;
    double goal_y = 0.0;
    if (!resolve_goal(goal_x, goal_y)) {
      return finish(false, started, goal_error_);
    }
    std::printf("  goal       : (%.2f, %.2f) in the map frame\n", goal_x, goal_y);

    if (!replan(goal_x, goal_y)) {
      return finish(false, started, plan_error_);
    }
    initial_plan_waypoints_ = plan_.size();
    initial_plan_length_ = plan_.length;
    std::printf(
      "  plan       : %zu waypoints, %.2f m long, from (%.2f, %.2f) to (%.2f, %.2f)\n",
      plan_.size(), plan_.length, plan_.xs.front(), plan_.ys.front(),
      plan_.xs.back(), plan_.ys.back());
    plan_publisher_->publish(to_path_message(plan_, "map"));

    if (options_.plan_only) {
      std::printf("  --plan-only: not driving, stopping here\n");
      return finish(true, started, "plan-only, nothing was driven");
    }
    return drive(goal_x, goal_y, started);
  }

private:
  bool laser_enabled() const {return options_.map_source != "map";}
  bool map_enabled() const {return options_.map_source != "laser";}

  void print_topics() const
  {
    std::printf("  topics     : the shim graph currently knows\n");
    for (const auto & topic : ros2_shim::graph::list_topics()) {
      std::printf(
        "    %-14s %-34s pub=%d sub=%d source=%s\n", topic.name.c_str(),
        topic.type.empty() ? "(unknown)" : topic.type.c_str(), topic.publishers,
        topic.subscribers, topic.source.c_str());
    }
  }

  void spin_for(double seconds)
  {
    const auto deadline = std::chrono::steady_clock::now() +
      std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
      node_->spin_once();
      sleep_ms(1.0);
    }
  }

  bool wait_for_topics()
  {
    const auto begin = std::chrono::steady_clock::now();
    const auto deadline = begin + std::chrono::duration<double>(options_.wait_topics);
    bool announced = false;
    while (std::chrono::steady_clock::now() < deadline) {
      node_->spin_once();
      if (grid_ && scan_ && odom_count_ > 0) {
        return true;
      }
      if (!announced && std::chrono::steady_clock::now() > begin + std::chrono::seconds(2)) {
        std::printf("  waiting    : /map=%s /scan=%s /odom=%s (is the daemon running?)\n",
          grid_ ? "ok" : "missing", scan_ ? "ok" : "missing",
          odom_count_ ? "ok" : "missing");
        announced = true;
      }
      sleep_ms(5.0);
    }
    std::printf("  waiting    : timed out after %.0f s: /map=%s /scan=%s /odom=%s\n",
      options_.wait_topics, grid_ ? "ok" : "missing", scan_ ? "ok" : "missing",
      odom_count_ ? "ok" : "missing");
    std::printf(
      "  hint       : start the Windows side with ros2_bridge.bat start, then check\n"
      "               `ros2 topic list` in a window that called bridge_env.bat\n");
    return false;
  }

  bool build_costmap()
  {
    if (map_enabled()) {
      if (!grid_) {
        costmap_error_ = "no /map has been received";
        return false;
      }
      if (!build_base_costmap(*grid_, costmap_, costmap_error_)) {
        return false;
      }
    } else {
      // Laser only: start from a completely unknown grid and let /scan fill it.
      costmap_.width = static_cast<int>(grid_->info.width);
      costmap_.height = static_cast<int>(grid_->info.height);
      costmap_.resolution = grid_->info.resolution;
      costmap_.origin_x = grid_->info.origin.position.x;
      costmap_.origin_y = grid_->info.origin.position.y;
      costmap_.cost.assign(
        static_cast<size_t>(costmap_.width) * costmap_.height, COST_UNKNOWN);
    }

    if (laser_enabled()) {
      if (!scan_) {
        costmap_error_ = "no /scan has been received";
        return false;
      }
      if (laser_.width != costmap_.width || laser_.height != costmap_.height) {
        laser_.reset(costmap_);
      }
      laser_.integrate(*scan_, pose_);
      merge_laser_overlay(laser_, costmap_);
    }

    inflate_costmap(
      costmap_, options_.inscribed_radius, options_.inflation_radius, options_.cost_scaling);
    clear_robot_footprint(costmap_, pose_, options_.footprint_radius);
    return true;
  }

  bool replan(double goal_x, double goal_y)
  {
    if (!plan_path(costmap_, pose_, goal_x, goal_y, plan_, plan_error_)) {
      return false;
    }
    path_index_ = 0;
    goal_x_ = goal_x;
    goal_y_ = goal_y;
    return true;
  }  // "auto": the furthest reachable free cell, so the demo does something
  // interesting even on a randomly generated world.
  bool resolve_goal(double & goal_x, double & goal_y)
  {
    if (options_.goal != "auto") {
      const std::string text = options_.goal;
      const std::size_t comma = text.find(',');
      if (comma == std::string::npos ||
        !parse_double(text.substr(0, comma), goal_x) ||
        !parse_double(text.substr(comma + 1), goal_y))
      {
        goal_error_ = "--goal expects 'X,Y' or 'auto'";
        return false;
      }
      return true;
    }

    struct Candidate
    {
      double x;
      double y;
      double distance;
    };
    std::vector<Candidate> candidates;
    const int stride = 8;
    for (int my = 0; my < costmap_.height; my += stride) {
      for (int mx = 0; mx < costmap_.width; mx += stride) {
        if (costmap_.at(mx, my) != COST_FREE) {
          continue;
        }
        const double wx = costmap_.cell_center_x(mx);
        const double wy = costmap_.cell_center_y(my);
        const double d = distance(pose_.x, pose_.y, wx, wy);
        if (d < 4.0 || d > 12.0) {
          continue;
        }
        candidates.push_back(Candidate{wx, wy, d});
      }
    }
    std::sort(
      candidates.begin(), candidates.end(),
      [](const Candidate & a, const Candidate & b) {return a.distance > b.distance;});

    Plan plan;
    std::string error;
    const size_t attempts = std::min<size_t>(candidates.size(), 25);
    for (size_t i = 0; i < attempts; ++i) {
      if (plan_path(costmap_, pose_, candidates[i].x, candidates[i].y, plan, error) &&
        plan.length > 3.0)
      {
        goal_x = candidates[i].x;
        goal_y = candidates[i].y;
        std::printf(
          "  auto goal  : picked (%.2f, %.2f), %.2f m away, %zu of %zu candidates tried\n",
          goal_x, goal_y, candidates[i].distance, i + 1, candidates.size());
        return true;
      }
    }
    goal_error_ = "none of the " + std::to_string(candidates.size()) +
      " goal candidate(s) was reachable, e.g. " + error;
    return false;
  }

  // Pure pursuit: follow the plan, slow down near the goal, publish /cmd_vel.
  int drive(
    double goal_x, double goal_y, const std::chrono::steady_clock::time_point & started)
  {
    const double period = 1.0 / options_.control_rate;
    const double start_x = pose_.x;
    const double start_y = pose_.y;
    auto last_replan = std::chrono::steady_clock::now();
    auto last_report = last_replan;
    auto last_progress = last_replan;
    Pose2D last_progress_pose = pose_;
    int stalled_cycles = 0;

    std::printf(
      "  driving    : %.0f Hz control loop, %.2f m/s limit, replanning every %.1f s\n",
      options_.control_rate, options_.max_linear, options_.replan_period);
    while (true) {
      const auto now = std::chrono::steady_clock::now();
      const double elapsed = std::chrono::duration<double>(now - started).count();
      if (elapsed > options_.timeout) {
        publish_stop();
        return finish(false, started, "timeout before reaching the goal");
      }

      node_->spin_once();

      const double remaining = distance(pose_.x, pose_.y, goal_x, goal_y);
      if (remaining <= options_.tolerance) {
        publish_stop();
        const double travelled = distance(start_x, start_y, pose_.x, pose_.y);
        std::printf(
          "  reached    : (%.2f, %.2f) after %.1f s, %.2f m from the start, "
          "%.2f m from the goal\n",
          pose_.x, pose_.y, elapsed, travelled, remaining);
        if (travelled < 0.1) {
          std::printf(
            "  warning    : the robot barely moved; check the /cmd_vel uplink\n");
        }
        return finish(true, started, "");
      }

      if (std::chrono::duration<double>(now - last_replan).count() >= options_.replan_period) {
        build_costmap();
        if (replan(goal_x, goal_y)) {
          plan_publisher_->publish(to_path_message(plan_, "map"));
        }
        last_replan = now;
      }

      const std::pair<double, double> command = compute_command();
      geometry_msgs::msg::Twist twist;
      twist.linear.x = command.first;
      twist.angular.z = command.second;
      cmd_publisher_->publish(twist);

      if (options_.verbose) {
        std::printf(
          "    t=%6.1f pose=(%6.2f,%6.2f,%5.2f) waypoint=%zu/%zu remaining=%.2f "
          "cmd=(%.2f,%.2f)\n",
          elapsed, pose_.x, pose_.y, pose_.yaw, path_index_, plan_.size(), remaining,
          command.first, command.second);
      } else if (std::chrono::duration<double>(now - last_report).count() >= 1.0) {
        std::printf(
          "    t=%5.1fs pose=(%6.2f,%6.2f,%5.2f) waypoint=%zu/%zu remaining=%.2fm "
          "cmd=(%.2f,%.2f)\n",
          elapsed, pose_.x, pose_.y, pose_.yaw, path_index_, plan_.size(), remaining,
          command.first, command.second);
        last_report = now;
      }

      if (std::chrono::duration<double>(now - last_progress).count() >= 5.0) {
        if (distance(last_progress_pose.x, last_progress_pose.y, pose_.x, pose_.y) < 0.05) {
          ++stalled_cycles;
          std::printf(
            "  warning    : no odometry progress for 5 s (stuck at %.2f, %.2f) - is the\n"
            "               car receiving /cmd_vel on the Ubuntu side?\n", pose_.x, pose_.y);
        } else {
          stalled_cycles = 0;
        }
        last_progress = now;
        last_progress_pose = pose_;
        if (stalled_cycles >= 3) {
          publish_stop();
          return finish(false, started, "the robot stopped making progress");
        }
      }

      sleep_ms(std::max(1.0, period * 1000.0));
    }
  }  // Pure pursuit: chase the point that sits `lookahead` metres ahead on the plan.
  std::pair<double, double> compute_command()
  {
    const size_t count = plan_.size();
    // Advance the closest waypoint monotonically so the robot never drives back.
    while (path_index_ + 1 < count &&
      distance(
        pose_.x, pose_.y, plan_.xs[path_index_ + 1], plan_.ys[path_index_ + 1]) <=
      distance(pose_.x, pose_.y, plan_.xs[path_index_], plan_.ys[path_index_]))
    {
      ++path_index_;
    }

    // Lookahead point: the first waypoint at least `lookahead` metres along.
    size_t target = path_index_;
    double arc = 0.0;
    while (target + 1 < count && arc < options_.lookahead) {
      arc += distance(
        plan_.xs[target], plan_.ys[target], plan_.xs[target + 1], plan_.ys[target + 1]);
      ++target;
    }

    const double dx = plan_.xs[target] - pose_.x;
    const double dy = plan_.ys[target] - pose_.y;
    const double heading_error = normalize_angle(std::atan2(dy, dx) - pose_.yaw);
    const double remaining = distance(pose_.x, pose_.y, goal_x_, goal_y_);
    const double lookahead_distance = std::max(0.2, std::hypot(dx, dy));

    double linear = 0.0;
    double angular = 0.0;
    if (std::fabs(heading_error) > 1.2 && remaining > 0.6) {
      // Turn on the spot when the target sits far to the side.
      angular = std::copysign(
        std::min(options_.max_angular, 1.0 + std::fabs(heading_error)), heading_error);
    } else {
      linear = std::min(options_.max_linear, 1.2 * remaining);
      // Ease off while turning and approach the goal slowly.
      linear *= std::max(0.35, 1.0 - 0.5 * std::fabs(heading_error) / PI);
      angular = 2.0 * linear * std::sin(heading_error) / lookahead_distance;
      angular = std::max(-options_.max_angular, std::min(options_.max_angular, angular));
    }
    return {linear, angular};
  }

  void publish_stop()
  {
    geometry_msgs::msg::Twist twist;
    twist.linear.x = 0.0;
    twist.angular.z = 0.0;
    for (int i = 0; i < 3; ++i) {  // the uplink drops nothing, three is just polite
      cmd_publisher_->publish(twist);
      node_->spin_once();
      sleep_ms(20.0);
    }
  }

  int finish(
    bool reached, const std::chrono::steady_clock::time_point & started,
    const std::string & reason)
  {
    const double elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - started).count();
    if (!reason.empty()) {
      std::printf(
        "  result     : %s (%s)\n", reached ? "OK" : "FAILED", reason.c_str());
    } else {
      std::printf("  result     : %s\n", reached ? "OK" : "FAILED");
    }
    if (!options_.report.empty()) {
      write_report(options_.report, reached, elapsed, reason);
    }
    rclcpp::shutdown();
    return reached ? 0 : 1;
  }

  void write_report(
    const std::string & path, bool reached, double elapsed, const std::string & reason)
  {
    std::ofstream out(path);
    if (!out) {
      std::fprintf(stderr, "warning: could not write the report to %s\n", path.c_str());
      return;
    }
    const double travelled = distance(start_pose_.x, start_pose_.y, pose_.x, pose_.y);
    out << "{\n";
    out << "  \"reached\": " << (reached ? "true" : "false") << ",\n";
    out << "  \"reason\": \"" << reason << "\",\n";
    out << "  \"elapsed_s\": " << elapsed << ",\n";
    out << "  \"map_source\": \"" << options_.map_source << "\",\n";
    out << "  \"map\": {\"width\": " << costmap_.width << ", \"height\": " << costmap_.height
        << ", \"resolution\": " << costmap_.resolution
        << ", \"lethal\": " << costmap_.count(COST_LETHAL)
        << ", \"inscribed\": " << costmap_.count(COST_INSCRIBED)
        << ", \"unknown\": " << costmap_.count(COST_UNKNOWN) << "},\n";
    out << "  \"scan\": {\"scans\": " << laser_.scans << ", \"beams\": " << laser_.beams
        << ", \"free_marks\": " << laser_.marked_free
        << ", \"occupied_marks\": " << laser_.marked_occupied << "},\n";
    out << "  \"plan\": {\"waypoints\": " << initial_plan_waypoints_
        << ", \"length_m\": " << initial_plan_length_
        << ", \"last_replan_waypoints\": " << plan_.size()
        << ", \"last_replan_length_m\": " << plan_.length << "},\n";
    out << "  \"goal\": [" << goal_x_ << ", " << goal_y_ << "],\n";
    out << "  \"start_pose\": [" << start_pose_.x << ", " << start_pose_.y << "],\n";
    out << "  \"final_pose\": [" << pose_.x << ", " << pose_.y << ", " << pose_.yaw << "],\n";
    out << "  \"travelled_m\": " << travelled << ",\n";
    out << "  \"remaining_m\": " << distance(pose_.x, pose_.y, goal_x_, goal_y_) << ",\n";
    out << "  \"messages\": {\"map\": " << map_count_ << ", \"scan\": " << scan_count_
        << ", \"odom\": " << odom_count_ << "}\n";
    out << "}\n";
    std::printf("  report     : %s\n", path.c_str());
  }

  Options options_;

  rclcpp::Node::SharedPtr node_;
  rclcpp::Subscription<nav_msgs::msg::OccupancyGrid>::SharedPtr map_subscription_;
  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr scan_subscription_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_subscription_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_publisher_;
  rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr plan_publisher_;

  nav_msgs::msg::OccupancyGrid::ConstSharedPtr grid_;
  sensor_msgs::msg::LaserScan::ConstSharedPtr scan_;
  Pose2D pose_;
  Pose2D start_pose_;
  size_t map_count_ = 0;
  size_t scan_count_ = 0;
  size_t odom_count_ = 0;

  Costmap costmap_;
  LaserOverlay laser_;
  Plan plan_;
  size_t initial_plan_waypoints_ = 0;
  double initial_plan_length_ = 0.0;
  size_t path_index_ = 0;
  double goal_x_ = 0.0;
  double goal_y_ = 0.0;

  std::string costmap_error_;
  std::string plan_error_;
  std::string goal_error_;
};

}  // namespace

int main(int argc, char ** argv)
{
  // Keep our own lines in order with the shim's stderr logging.
  std::setvbuf(stdout, nullptr, _IOLBF, 0);

  Options options;
  if (!parse_args(argc, argv, options)) {
    return 2;
  }
  Nav2Demo demo(options);
  return demo.run(argc, argv);
}