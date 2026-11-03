// The serialization fast paths, and the slot sizes that decide whether a
// message fits at all.
//
// Three things changed and all three are invisible when they are wrong:
//
//   1. `serialize()` sizes the buffer before writing it (one walk to count,
//      one to write) and `serializeInto()` reuses the caller's buffer. The
//      bytes have to be exactly the same as before - the wire format is
//      shared with the other side of a bridge, so "the same but faster" is
//      the only acceptable outcome.
//   2. Primitive arrays are written with one memcpy instead of one call per
//      element. A laser scan has two float arrays and an odometry has two
//      36-element double arrays; the byte order of a bulk copy is the host's,
//      which is why the fast path only exists on a little endian host.
//   3. The reading side does the same, so a scan survives the round trip.
//
// The slot sizing is checked here too: an image publisher used to ask for
// the default slot - 512 KB through the bridge environment - and every
// 640x480 RGB8 frame (921,600 bytes) was dropped without a word. The rule is
// now "the message type decides", and this file pins which type gets which
// size.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/node.hpp"
#include "rclcpp/publisher.hpp"
#include "rclcpp/shm_config.hpp"
#include "ros2_shim/serialization.hpp"
#include "shm_transport/shm_channel.hpp"

#include "geometry_msgs/msg/twist.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"

namespace
{

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char * what)
{
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::fprintf(stderr, "FAIL %s\n", what);
  }
}

sensor_msgs::msg::LaserScan makeScan(std::size_t beams)
{
  sensor_msgs::msg::LaserScan message;
  message.header.stamp.sec = 12345;
  message.header.stamp.nanosec = 67890;
  message.header.frame_id = "laser";
  message.angle_min = -3.14159f;
  message.angle_max = 3.14159f;
  message.angle_increment = static_cast<float>(6.28318 / beams);
  message.time_increment = 0.0f;
  message.scan_time = 0.1f;
  message.range_min = 0.05f;
  message.range_max = 12.0f;
  message.ranges.resize(beams);
  message.intensities.resize(beams);
  for (std::size_t i = 0; i < beams; ++i) {
    message.ranges[i] = static_cast<float>(i % 100) * 0.1f + 0.2f;
    message.intensities[i] = static_cast<float>(i % 255);
  }
  return message;
}

nav_msgs::msg::Odometry makeOdometry()
{
  nav_msgs::msg::Odometry message;
  message.header.stamp.sec = 7;
  message.header.frame_id = "odom";
  message.child_frame_id = "base";
  message.pose.pose.position.x = 1.5;
  message.pose.pose.orientation.w = 1.0;
  for (std::size_t i = 0; i < 36; ++i) {
    message.pose.covariance[i] = static_cast<double>(i) * 0.001;
    message.twist.covariance[i] = static_cast<double>(i) * 0.002;
  }
  return message;
}

// 1. The size the counter predicts is the size that comes out.
void testSizePrediction()
{
  const auto scan = makeScan(720);
  const auto bytes = ros2_shim::serialize(scan);
  check(
    ros2_shim::detail::serialized_size_of(scan) == bytes.size(),
    "serialized_size_of(LaserScan) matches the encoded length");

  const auto odometry = makeOdometry();
  const auto odomBytes = ros2_shim::serialize(odometry);
  check(
    ros2_shim::detail::serialized_size_of(odometry) == odomBytes.size(),
    "serialized_size_of(Odometry) matches the encoded length");

  geometry_msgs::msg::Twist twist;
  twist.linear.x = 0.25;
  twist.angular.z = -0.5;
  check(
    ros2_shim::detail::serialized_size_of(twist) == ros2_shim::serialize(twist).size(),
    "serialized_size_of(Twist) matches the encoded length");
}

// 2. `serializeInto()` writes the same bytes as `serialize()`, and a second
// call into the same buffer is not polluted by the first one.
void testSerializeIntoMatches()
{
  const auto scan = makeScan(128);
  const auto expected = ros2_shim::serialize(scan);

  std::vector<std::uint8_t> buffer;
  ros2_shim::serializeInto(scan, buffer);
  check(buffer == expected, "serializeInto() writes the same bytes as serialize()");

  // Reuse: the second message must not start with the first one's bytes.
  const auto other = makeScan(64);
  ros2_shim::serializeInto(other, buffer);
  const auto otherExpected = ros2_shim::serialize(other);
  check(buffer == otherExpected, "serializeInto() reuses the buffer without leftovers");
  check(
    buffer.capacity() >= otherExpected.size(),
    "the reused buffer kept its capacity");
}

// 3. The round trip through the bulk paths keeps every value.
void testRoundTrip()
{
  const auto scan = makeScan(720);
  const auto bytes = ros2_shim::serialize(scan);
  sensor_msgs::msg::LaserScan decoded;
  check(
    ros2_shim::deserialize(bytes.data(), bytes.size(), decoded),
    "a laser scan decodes");
  check(decoded.ranges.size() == scan.ranges.size(), "the range count survives");
  check(decoded.intensities.size() == scan.intensities.size(), "the intensity count survives");
  bool same = true;
  for (std::size_t i = 0; i < scan.ranges.size(); ++i) {
    same = same && (decoded.ranges[i] == scan.ranges[i]);
    same = same && (decoded.intensities[i] == scan.intensities[i]);
  }
  check(same, "every range and intensity survives the round trip");
  check(decoded.header.frame_id == scan.header.frame_id, "the frame id survives");

  const auto odometry = makeOdometry();
  const auto odomBytes = ros2_shim::serialize(odometry);
  nav_msgs::msg::Odometry decodedOdom;
  check(
    ros2_shim::deserialize(odomBytes.data(), odomBytes.size(), decodedOdom),
    "an odometry decodes");
  check(
    decodedOdom.pose.covariance.size() == 36 && decodedOdom.twist.covariance.size() == 36,
    "both covariance arrays keep their length");
  bool covarianceSame = true;
  for (std::size_t i = 0; i < 36; ++i) {
    covarianceSame = covarianceSame && (decodedOdom.pose.covariance[i] == odometry.pose.covariance[i]);
    covarianceSame = covarianceSame && (decodedOdom.twist.covariance[i] == odometry.twist.covariance[i]);
  }
  check(covarianceSame, "every covariance value survives the round trip");
}

// 4. A truncated array is a failed decode, not a half-filled one. The bulk
// read path counts whole elements, so a count that does not fit is caught
// before anything is copied.
void testTruncatedArrayFails()
{
  const auto scan = makeScan(64);
  auto bytes = ros2_shim::serialize(scan);
  bytes.resize(bytes.size() - 8);

  sensor_msgs::msg::LaserScan decoded;
  check(
    !ros2_shim::deserialize(bytes.data(), bytes.size(), decoded),
    "a truncated scan fails to decode");
}

// 5. The slot sizes: a type decides, and the small-channel rules still win.
void testSlotSizes()
{
  using rclcpp::shim::default_slot_size;
  using rclcpp::shim::image_slot_size;
  using rclcpp::shim::parameter_slot_size;
  using rclcpp::shim::pointcloud_slot_size;
  using rclcpp::shim::slot_size_for_topic;
  using rclcpp::shim::slot_size_for_type;

  check(
    slot_size_for_type("sensor_msgs/msg/Image", "/camera/image_raw") == image_slot_size(),
    "an image topic gets the image slot");
  check(
    image_slot_size() >= 6u * 1024u * 1024u,
    "the image slot fits a 1080p RGB8 frame (6.2 MB)");
  check(
    slot_size_for_type("sensor_msgs/msg/PointCloud2", "/lidar/points") == pointcloud_slot_size(),
    "a point cloud topic gets the point cloud slot");
  check(
    slot_size_for_type("sensor_msgs/msg/CompressedImage", "/camera/image_raw/compressed") ==
    default_slot_size(),
    "a compressed image keeps the default slot");
  check(
    slot_size_for_type("sensor_msgs/msg/LaserScan", "/scan") == default_slot_size(),
    "a laser scan keeps the default slot");
  check(
    slot_size_for_type("", "/scan") == slot_size_for_topic("/scan"),
    "no type means the name rules decide");
  check(
    slot_size_for_type("sensor_msgs/msg/Image", "/rosout") == parameter_slot_size(),
    "the small-channel rules still win over the type");
  check(
    slot_size_for_type("sensor_msgs/msg/Image", "/map") == image_slot_size(),
    "an image on a map-named topic still gets the image slot");
  check(
    slot_size_for_topic("/map") != default_slot_size(),
    "the name rules are unchanged for grids");
}

// 6. The runtime case the slot sizes exist for: a camera publisher whose
// topic name says nothing about size, sending a frame that is bigger than
// the default slot. The frame used to be refused in silence - `publish()`
// returned false and nothing said so.
void testImagePublisherFits()
{
  auto node = std::make_shared<rclcpp::Node>("bulk_image_node", "/ctest_bulk_images");
  auto publisher = node->create_publisher<sensor_msgs::msg::Image>(
    "/camera/image_raw", rclcpp::QoS(1));
  auto channel = publisher->get_channel();
  check(channel != nullptr, "the image publisher got a channel");
  if (!channel) {
    return;
  }
  check(
    channel->slot_size() == rclcpp::shim::image_slot_size(),
    "the image channel is sized by the message type");

  sensor_msgs::msg::Image image;
  image.header.frame_id = "camera";
  image.height = 480;
  image.width = 640;
  image.encoding = "rgb8";
  image.is_bigendian = 0;
  image.step = 640 * 3;
  image.data.resize(static_cast<std::size_t>(480) * 640 * 3, 0xAB);

  publisher->publish(image);
  check(
    publisher->oversized_count() == 0,
    "a 640x480 RGB8 frame fits the image channel");

  // The frame really is in the channel, and it is the frame that went in.
  std::vector<std::uint8_t> buffer(channel->slot_size());
  std::size_t size = 0;
  check(
    channel->subscribe(buffer.data(), buffer.size(), &size, 500),
    "the frame comes back out of the channel");
  const auto expected = ros2_shim::serialize(image);
  check(size == expected.size(), "the frame kept its size on the way through");
  check(
    size == expected.size() && std::memcmp(buffer.data(), expected.data(), size) == 0,
    "the bytes on the channel are the bytes that were published");

  // And a frame that does not fit is reported, not swallowed.
  sensor_msgs::msg::Image huge = image;
  huge.width = 4096;
  huge.height = 4096;
  huge.step = 4096 * 3;
  huge.data.resize(static_cast<std::size_t>(4096) * 4096 * 3, 0xCD);
  publisher->publish(huge);
  check(
    publisher->oversized_count() == 1,
    "a frame that does not fit is counted as dropped");
}

}  // namespace

int main()
{
  testSizePrediction();
  testSerializeIntoMatches();
  testRoundTrip();
  testTruncatedArrayFails();
  testSlotSizes();
  testImagePublisherFits();

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
