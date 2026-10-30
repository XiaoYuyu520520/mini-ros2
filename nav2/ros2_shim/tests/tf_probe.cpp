// Live probe: does a shim *node* see the bridged /tf, /tf_static and /scan?
//
// This is not a ctest - it needs the gRPC bridge daemon and a peer on the
// Ubuntu side, so it is run by hand:
//
//     call bridge\windows\bridge_env.bat
//     build\slam_toolbox_port\..\ros2_shim\tf_probe.exe --duration 20
//
// It exists because `ros2 topic echo` reads the shared-memory channel through
// its own code path, so a message arriving there says nothing about whether a
// node's subscription (the path every ported package uses) got it.
#include "geometry_msgs/msg/point_stamped.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"
#include "tf2_msgs/msg/tf_message.hpp"
#include "tf2_ros/buffer.hpp"
#include "tf2_ros/transform_listener.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>

// Counts what the listener itself is handed, which is what separates "the
// topic never arrived" from "the buffer the listener fills is not the one
// being read".
class CountingListener : public tf2_ros::TransformListener
{
public:
  using tf2_ros::TransformListener::TransformListener;

  void subscription_callback(
    tf2_msgs::msg::TFMessage::ConstSharedPtr message, bool is_static) override
  {
    if (is_static) {
      ++static_callbacks;
    } else {
      ++callbacks;
    }
    for (const auto & transform : message->transforms) {
      ++frames[transform.header.frame_id + " -> " + transform.child_frame_id];
    }
    tf2_ros::TransformListener::subscription_callback(message, is_static);
  }

  int callbacks = 0;
  int static_callbacks = 0;
  std::map<std::string, int> frames;
};

int main(int argc, char ** argv)
{
  double duration = 20.0;
  // The frame a costmap would transform a sensor origin into; the local
  // costmap's is `odom`, the global one's `map`.
  std::string target_frame = "odom";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--duration" && i + 1 < argc) {
      duration = std::strtod(argv[++i], nullptr);
    }
    if (arg == "--target-frame" && i + 1 < argc) {
      target_frame = argv[++i];
    }
  }

  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("tf_probe");

  int scan_msgs = 0;
  std::map<std::string, int> scan_frames;

  // /tf and /tf_static are read through the listener alone. A channel object
  // carries one reader cursor, and the node keeps one per topic, so a second
  // subscription to the same topic would take the messages from the first:
  // the buffer would stay empty and this probe would report a frame tree that
  // is not there.
  // The same buffer + listener pair a costmap builds, so the probe measures
  // the path the costmap layers use rather than a look-alike.
  auto buffer = std::make_shared<tf2_ros::Buffer>(node->get_clock());
  auto listener = std::make_shared<CountingListener>(*buffer, node, false);
  int origin_reports = 0;

  auto scan_sub = node->create_subscription<sensor_msgs::msg::LaserScan>(
    "/scan", rclcpp::SensorDataQoS(),
    [&](sensor_msgs::msg::LaserScan::ConstSharedPtr message) {
      ++scan_msgs;
      ++scan_frames[message->header.frame_id];
      if (++origin_reports > 5 || message->header.frame_id.empty()) {
        return;
      }
      // What the costmap's observation buffer does with every scan: take a
      // point at the sensor, stamped, in the sensor frame, and move it into
      // the costmap frame. A costmap whose observations all sit at (0, 0)
      // cannot raytrace, and reports it as "Sensor origin ... is out of map
      // bounds".
      geometry_msgs::msg::PointStamped local;
      geometry_msgs::msg::PointStamped global;
      local.header = message->header;
      try {
        buffer->transform(local, global, target_frame, tf2::durationFromSec(0.1));
        std::printf(
          "PROBE origin %s -> %s: (%.3f, %.3f, %.3f)\n", message->header.frame_id.c_str(),
          target_frame.c_str(), global.point.x, global.point.y, global.point.z);
      } catch (const tf2::TransformException & ex) {
        std::printf("PROBE origin transform failed: %s\n", ex.what());
      }
      try {
        const auto transform = buffer->lookupTransform(
          target_frame, message->header.frame_id,
          tf2::timePointFromRosTime(message->header.stamp));
        std::printf(
          "PROBE lookup %s <- %s at %.3f: (%.3f, %.3f, %.3f)\n", target_frame.c_str(),
          message->header.frame_id.c_str(),
          static_cast<double>(message->header.stamp.sec) + message->header.stamp.nanosec * 1e-9,
          transform.transform.translation.x, transform.transform.translation.y,
          transform.transform.translation.z);
      } catch (const tf2::TransformException & ex) {
        std::printf("PROBE lookup failed: %s\n", ex.what());
      }
    });

  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
    std::chrono::duration<double>(duration));
  while (rclcpp::ok() && std::chrono::steady_clock::now() < deadline) {
    node->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::printf(
    "PROBE listener /tf=%d /tf_static=%d /scan=%d\n", listener->callbacks,
    listener->static_callbacks, scan_msgs);
  std::printf("PROBE buffer frames: %s\n", buffer->allFramesAsString().c_str());
  for (const auto & entry : listener->frames) {
    std::printf("PROBE tf frame %s x%d\n", entry.first.c_str(), entry.second);
  }
  for (const auto & entry : scan_frames) {
    std::printf("PROBE scan frame %s x%d\n", entry.first.c_str(), entry.second);
  }
  rclcpp::shutdown();
  return 0;
}
