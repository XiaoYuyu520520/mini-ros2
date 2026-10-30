// Reproduces nav2_amcl's transform setup on the shim, step by step, so the
// "frame 'laser' does not exist" failure can be pinned to one of them.
//
// Not a ctest: it needs the bridge daemon up with /tf and /tf_static in shared
// memory. Run it by hand:
//
//     call bridge\windows\bridge_env.bat
//     nav2\ros2_shim\build\tf_listener_probe.exe --mode lifecycle
//
// Modes:
//   plain      rclcpp::Node + tf2_ros::TransformListener(buffer, node, true)
//   lifecycle  rclcpp_lifecycle::LifecycleNode + nav2::create_transform_buffer /
//              create_transform_listener  (the nav2_ros_common helpers AMCL uses)
//   owned      as lifecycle, but the buffer/listener are held through the same
//              shared_ptr types AmclNode declares
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "nav2_ros_common/tf2_factories.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"
#include "tf2_ros/buffer.hpp"
#include "tf2_ros/transform_listener.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

namespace
{

const char * kUsage =
  "usage: tf_listener_probe [--mode plain|lifecycle|owned] [--duration S] [--target FRAME]\n"
  "                         [--source FRAME]\n";

struct Fixture
{
  rclcpp::Node::SharedPtr node;
  rclcpp::CallbackGroup::SharedPtr callback_group;
  tf2_ros::Buffer::SharedPtr buffer;
  std::shared_ptr<tf2_ros::TransformListener> listener;
};

Fixture makeFixture(const std::string & mode)
{
  Fixture fixture;
  if (mode == "plain") {
    fixture.node = std::make_shared<rclcpp::Node>("tf_listener_probe");
    fixture.buffer = std::make_shared<tf2_ros::Buffer>(fixture.node->get_clock());
    fixture.listener =
      std::make_shared<tf2_ros::TransformListener>(*fixture.buffer, fixture.node, true);
    return fixture;
  }

  auto lifecycle = std::make_shared<rclcpp_lifecycle::LifecycleNode>("tf_listener_probe");
  fixture.node = lifecycle;
  fixture.callback_group = lifecycle->create_callback_group(
    rclcpp::CallbackGroupType::MutuallyExclusive, false);
  fixture.buffer = nav2::create_transform_buffer(lifecycle, fixture.callback_group);
  fixture.listener = nav2::create_transform_listener(*fixture.buffer, lifecycle.get(), true);
  return fixture;
}

}  // namespace

int main(int argc, char ** argv)
{
  std::string mode = "lifecycle";
  std::string target = "base_link";
  std::string source = "laser";
  double duration = 10.0;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto next = [&](const char * option) -> const char * {
        if (i + 1 >= argc) {
          std::fprintf(stderr, "[tf_listener_probe] missing value for %s\n", option);
          std::exit(2);
        }
        return argv[++i];
      };
    if (arg == "--help" || arg == "-h") {
      std::fputs(kUsage, stdout);
      return 0;
    } else if (arg == "--mode") {
      mode = next("--mode");
    } else if (arg == "--duration") {
      duration = std::strtod(next("--duration"), nullptr);
    } else if (arg == "--target") {
      target = next("--target");
    } else if (arg == "--source") {
      source = next("--source");
    }
  }

  rclcpp::init(argc, argv);
  Fixture fixture = makeFixture(mode);

  int scan_msgs = 0;
  auto scan_sub = fixture.node->create_subscription<sensor_msgs::msg::LaserScan>(
    "/scan", rclcpp::QoS(rclcpp::KeepLast(10)).best_effort(),
    [&](sensor_msgs::msg::LaserScan::ConstSharedPtr) {++scan_msgs;});

  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
    std::chrono::duration<double>(duration));
  while (rclcpp::ok() && std::chrono::steady_clock::now() < deadline) {
    fixture.node->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::printf("PROBE mode=%s scan=%d odom->base_link=%d base_link->laser=%d\n",
    mode.c_str(), scan_msgs,
    fixture.buffer->canTransform("odom", "base_link") ? 1 : 0,
    fixture.buffer->canTransform("base_link", "laser") ? 1 : 0);
  std::fflush(stdout);

  geometry_msgs::msg::PoseStamped ident;
  ident.header.frame_id = source;
  ident.header.stamp = rclcpp::Time();
  geometry_msgs::msg::PoseStamped out;
  try {
    fixture.buffer->transform(ident, out, target, tf2::Duration::zero());
    std::printf("PROBE zero-stamp transform %s -> %s ok: %.4f %.4f\n",
      source.c_str(), target.c_str(), out.pose.position.x, out.pose.position.y);
  } catch (const tf2::TransformException & e) {
    std::printf("PROBE zero-stamp transform %s -> %s FAILED: %s\n",
      source.c_str(), target.c_str(), e.what());
  }

  ident.header.stamp = fixture.node->now();
  try {
    fixture.buffer->transform(ident, out, target, tf2::Duration::zero());
    std::printf("PROBE now-stamp transform %s -> %s ok: %.4f %.4f\n",
      source.c_str(), target.c_str(), out.pose.position.x, out.pose.position.y);
  } catch (const tf2::TransformException & e) {
    std::printf("PROBE now-stamp transform %s -> %s FAILED: %s\n",
      source.c_str(), target.c_str(), e.what());
  }
  std::fflush(stdout);

  rclcpp::shutdown();
  return 0;
}
