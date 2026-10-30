#include "geometry_msgs/msg/pose_stamped.hpp"
#include "geometry_msgs/msg/pose_with_covariance.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "nav2_msgs/msg/costmap.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "nav_msgs/msg/path.hpp"
#include "ros2_shim/serialization.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"
#include "shm_transport/shm_channel.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace ros2_shim;

namespace
{

int g_failures = 0;

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::printf("  FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
      ++g_failures; \
    } \
  } while (0)

geometry_msgs::msg::PoseStamped make_pose(double x, double y, const std::string & frame)
{
  geometry_msgs::msg::PoseStamped pose;
  pose.header.frame_id = frame;
  pose.header.stamp.sec = 42;
  pose.header.stamp.nanosec = 123456789u;
  pose.pose.position.x = x;
  pose.pose.position.y = y;
  pose.pose.position.z = 0.0;
  pose.pose.orientation.x = 0.0;
  pose.pose.orientation.y = 0.0;
  pose.pose.orientation.z = 0.1;
  pose.pose.orientation.w = 0.995;
  return pose;
}

bool same_pose(const geometry_msgs::msg::PoseStamped & a, const geometry_msgs::msg::PoseStamped & b)
{
  return a.header.frame_id == b.header.frame_id && a.header.stamp.sec == b.header.stamp.sec &&
         a.header.stamp.nanosec == b.header.stamp.nanosec &&
         a.pose.position.x == b.pose.position.x && a.pose.position.y == b.pose.position.y &&
         a.pose.position.z == b.pose.position.z && a.pose.orientation.z == b.pose.orientation.z &&
         a.pose.orientation.w == b.pose.orientation.w;
}

void test_roundtrip_path()
{
  std::printf("[test] serialize/deserialize nav_msgs::msg::Path\n");
  nav_msgs::msg::Path path;
  path.header.frame_id = "map";
  path.header.stamp.sec = 7;
  path.poses.push_back(make_pose(1.0, 2.0, "map"));
  path.poses.push_back(make_pose(3.5, -4.25, "map"));
  path.poses.push_back(make_pose(0.0, 0.0, "map"));

  const auto bytes = serialize(path);
  nav_msgs::msg::Path restored;
  CHECK(deserialize(bytes, restored));
  CHECK(restored.header.frame_id == "map");
  CHECK(restored.header.stamp.sec == 7);
  CHECK(restored.poses.size() == 3);
  for (size_t i = 0; i < restored.poses.size(); ++i) {
    CHECK(same_pose(path.poses[i], restored.poses[i]));
  }
  std::printf("  serialized %zu bytes, %zu poses\n", bytes.size(), restored.poses.size());
}

void test_fixed_array_covariance()
{
  std::printf("[test] fixed-size array geometry_msgs::msg::PoseWithCovariance\n");
  geometry_msgs::msg::PoseWithCovariance pwc;
  pwc.pose = make_pose(9.0, 8.0, "odom").pose;
  for (size_t i = 0; i < pwc.covariance.size(); ++i) {
    pwc.covariance[i] = static_cast<double>(i) * 0.5;
  }
  const auto bytes = serialize(pwc);
  geometry_msgs::msg::PoseWithCovariance restored;
  CHECK(deserialize(bytes, restored));
  CHECK(restored.covariance[0] == 0.0);
  CHECK(restored.covariance[35] == 17.5);
  CHECK(restored.pose.position.x == 9.0);
  std::printf("  covariance[35]=%.1f, %zu bytes\n", restored.covariance[35], bytes.size());
}

void test_costmap_nested()
{
  std::printf("[test] nested + dynamic array nav2_msgs::msg::Costmap\n");
  nav2_msgs::msg::Costmap map;
  map.header.frame_id = "map";
  map.metadata.size_x = 4;
  map.metadata.size_y = 2;
  map.metadata.resolution = 0.05f;
  map.metadata.origin.position.x = -1.0;
  map.data = {0, 1, 2, 3, 4, 5, 6, 7};
  const auto bytes = serialize(map);
  nav2_msgs::msg::Costmap restored;
  CHECK(deserialize(bytes, restored));
  CHECK(restored.metadata.size_x == 4);
  CHECK(restored.metadata.resolution == 0.05f);
  CHECK(restored.data.size() == 8);
  CHECK(restored.data[7] == 7);
  std::printf("  data size=%zu, %zu bytes\n", restored.data.size(), bytes.size());
}

// A stack full of a pattern the checks below would notice, so a struct that
// leaves a field uninitialized reports the pattern instead of a leftover that
// happens to be zero today. This is what makes the failure a test rather than
// a coin flip.
void poison_stack()
{
  volatile uint8_t pattern[1024];
  for (size_t i = 0; i < sizeof(pattern); ++i) {
    pattern[i] = 0xA5;
  }
}

// A message a consumer builds by hand holds what ROS says a fresh message
// holds. rosidl zeroes every field, and consumers lean on it: slam_toolbox's
// `getOdomPose` fills in a TransformStamped's `header` and `rotation.w` and
// nothing else, and expects the transform that is left to be the identity one.
// A struct that instead hands back whatever the previous call left on the
// stack answers differently per stack layout -- which is how a mapping run
// reported the robot a third of the way to where it was, every pose of the lap.
void test_default_construction()
{
  std::printf("[test] a fresh message holds the values ROS promises\n");

  poison_stack();
  std_msgs::msg::Header header;
  CHECK(header.stamp.sec == 0);
  CHECK(header.stamp.nanosec == 0u);
  CHECK(header.frame_id.empty());

  poison_stack();
  geometry_msgs::msg::Vector3 vector;
  CHECK(vector.x == 0.0 && vector.y == 0.0 && vector.z == 0.0);

  // The quaternion is zeroed too, and all-zeros is not the identity rotation:
  // the convention is that a caller spells identity out as `rotation.w = 1`.
  poison_stack();
  geometry_msgs::msg::Quaternion quaternion;
  CHECK(
    quaternion.x == 0.0 && quaternion.y == 0.0 && quaternion.z == 0.0 &&
    quaternion.w == 0.0);

  poison_stack();
  geometry_msgs::msg::TransformStamped transform;
  CHECK(transform.transform.translation.x == 0.0);
  CHECK(transform.transform.translation.y == 0.0);
  CHECK(transform.transform.rotation.z == 0.0);
  transform.transform.rotation.w = 1.0;
  CHECK(transform.transform.rotation.w == 1.0);

  // The zeros have to reach the innermost field, not just the outermost one:
  // `pose.position` is a message of its own, built by this one.
  poison_stack();
  geometry_msgs::msg::PoseStamped pose;
  CHECK(pose.pose.position.x == 0.0 && pose.pose.position.z == 0.0);
  CHECK(pose.pose.orientation.w == 0.0);

  // Default-initializing a fixed-size array does not zero it.
  poison_stack();
  geometry_msgs::msg::PoseWithCovariance covariance;
  bool covariance_is_zero = true;
  for (double value : covariance.covariance) {
    covariance_is_zero = covariance_is_zero && value == 0.0;
  }
  CHECK(covariance_is_zero);

  poison_stack();
  sensor_msgs::msg::LaserScan scan;
  CHECK(scan.angle_min == 0.0f && scan.angle_max == 0.0f);
  CHECK(scan.range_min == 0.0f && scan.range_max == 0.0f);
  CHECK(scan.ranges.empty());

  poison_stack();
  nav_msgs::msg::OccupancyGrid grid;
  CHECK(grid.info.width == 0u && grid.info.height == 0u);
  CHECK(grid.info.resolution == 0.0f);
  CHECK(grid.info.origin.position.x == 0.0);
  CHECK(grid.data.empty());

  poison_stack();
  nav2_msgs::action::NavigateToPose::Feedback feedback;
  CHECK(feedback.number_of_recoveries == 0);
  CHECK(feedback.navigation_time.sec == 0);
  CHECK(feedback.distance_remaining == 0.0f);
  CHECK(feedback.current_pose.pose.position.x == 0.0);
}

void test_action_and_constants()
{
  std::printf("[test] action NavigateToPose feedback + result constants\n");
  nav2_msgs::action::NavigateToPose::Feedback fb;
  fb.current_pose = make_pose(1.5, 2.5, "map");
  fb.navigation_time.sec = 12;
  fb.navigation_time.nanosec = 34;
  fb.number_of_recoveries = 2;
  fb.distance_remaining = 3.25f;
  fb.position_tracking_error = 0.01f;
  fb.heading_tracking_error = 0.02f;
  const auto bytes = serialize(fb);
  nav2_msgs::action::NavigateToPose::Feedback restored;
  CHECK(deserialize(bytes, restored));
  CHECK(restored.navigation_time.sec == 12);
  CHECK(restored.navigation_time.nanosec == 34);
  CHECK(restored.number_of_recoveries == 2);
  CHECK(restored.distance_remaining == 3.25f);
  CHECK(restored.current_pose.header.frame_id == "map");
  CHECK(nav2_msgs::action::NavigateToPose::Result::TIMEOUT == 9003);
  CHECK(nav2_msgs::action::NavigateToPose::Result::NONE == 0);
  std::printf("  TIMEOUT=%u, %zu bytes\n", nav2_msgs::action::NavigateToPose::Result::TIMEOUT,
    bytes.size());
}

void test_shm_message_transport()
{
  std::printf("[test] nav_msgs::msg::Path over shared memory\n");
  const std::string name = "test_msg_path";
  const uint32_t slot_size = 65536;
  auto writer = shm_transport::ShmChannel::create(name, 64, slot_size);
  auto reader = shm_transport::ShmChannel::open(name);
  CHECK(writer != nullptr && reader != nullptr);
  if (!writer || !reader) {
    return;
  }

  nav_msgs::msg::Path sent;
  sent.header.frame_id = "map";
  for (int i = 0; i < 50; ++i) {
    sent.poses.push_back(make_pose(i * 0.5, -i * 0.25, "map"));
  }
  const auto bytes = serialize(sent);

  nav_msgs::msg::Path received;
  bool got = false;
  std::thread consumer([&]() {
      std::vector<uint8_t> buf(slot_size);
      size_t len = 0;
      if (reader->subscribe(buf.data(), buf.size(), &len, 5000)) {
        buf.resize(len);
        got = deserialize(buf, received);
      }
    });

  CHECK(writer->publish(bytes.data(), bytes.size()));
  consumer.join();

  CHECK(got);
  CHECK(received.poses.size() == 50);
  CHECK(received.header.frame_id == "map");
  if (received.poses.size() == 50) {
    CHECK(received.poses[49].pose.position.x == 24.5);
    CHECK(received.poses[49].pose.position.y == -12.25);
  }
  std::printf("  transferred %zu bytes, received %zu poses\n", bytes.size(),
    received.poses.size());
}

}  // namespace

int main()
{
  std::printf("=== ros2_shim message tests ===\n");
  test_roundtrip_path();
  test_default_construction();
  test_fixed_array_covariance();
  test_costmap_nested();
  test_action_and_constants();
  test_shm_message_transport();
  if (g_failures == 0) {
    std::printf("=== ALL TESTS PASSED ===\n");
    return 0;
  }
  std::printf("=== %d CHECK(S) FAILED ===\n", g_failures);
  return 1;
}
