// Cross-language interoperability harness.
// Modes:
//   dump <file>                        write canonical Path to file
//   check <file>                       read + verify canonical Path
//   pub <channel> <slot_size>          publish canonical Path repeatedly
//   sub <channel> <slot_size> <ms>     subscribe once and verify
//   sizes <topic>...                   print slot_size_for_topic for each name
#include "nav_msgs/msg/path.hpp"
#include "rclcpp/shm_config.hpp"
#include "ros2_shim/serialization.hpp"
#include "shm_transport/shm_channel.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace ros2_shim;

namespace
{

geometry_msgs::msg::PoseStamped pose_at(int i)
{
  geometry_msgs::msg::PoseStamped p;
  p.header.frame_id = "map";
  p.header.stamp.sec = 5;
  p.header.stamp.nanosec = 6;
  p.pose.position.x = 1.5 * i;
  p.pose.position.y = -2.5 * i;
  p.pose.position.z = 0.25 * i;
  p.pose.orientation.x = 0.1;
  p.pose.orientation.y = 0.2;
  p.pose.orientation.z = 0.3;
  p.pose.orientation.w = 0.9;
  return p;
}

nav_msgs::msg::Path canonical_path()
{
  nav_msgs::msg::Path path;
  path.header.frame_id = "map";
  path.header.stamp.sec = 5;
  path.header.stamp.nanosec = 6;
  for (int i = 0; i < 4; ++i) {
    path.poses.push_back(pose_at(i));
  }
  return path;
}

bool verify_canonical(const nav_msgs::msg::Path & path)
{
  if (path.header.frame_id != "map" || path.header.stamp.sec != 5 ||
    path.header.stamp.nanosec != 6 || path.poses.size() != 4)
  {
    return false;
  }
  for (int i = 0; i < 4; ++i) {
    const auto & p = path.poses[i];
    if (p.pose.position.x != 1.5 * i || p.pose.position.y != -2.5 * i ||
      p.pose.position.z != 0.25 * i || p.pose.orientation.w != 0.9 ||
      p.header.frame_id != "map")
    {
      return false;
    }
  }
  return true;
}

int dump(const std::string & path)
{
  const auto bytes = serialize(canonical_path());
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char *>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  std::printf("cpp dumped %zu bytes to %s\n", bytes.size(), path.c_str());
  return 0;
}

int check(const std::string & path)
{
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
    std::istreambuf_iterator<char>());
  if (bytes.empty()) {
    std::printf("cpp check: empty file %s\n", path.c_str());
    return 2;
  }
  nav_msgs::msg::Path msg;
  if (!deserialize(bytes, msg)) {
    std::printf("cpp check: deserialize failed\n");
    return 3;
  }
  if (!verify_canonical(msg)) {
    std::printf("cpp check: content mismatch\n");
    return 4;
  }
  std::printf("cpp check: OK (%zu bytes, %zu poses)\n", bytes.size(), msg.poses.size());
  return 0;
}

int pub(const std::string & channel, uint32_t slot_size)
{
  auto ch = shm_transport::ShmChannel::create(channel, 16, slot_size);
  if (!ch) {
    std::printf("cpp pub: create failed\n");
    return 2;
  }
  const auto bytes = serialize(canonical_path());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  uint64_t sent = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    if (ch->publish(bytes.data(), bytes.size())) {
      ++sent;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  std::printf("cpp pub: sent %llu messages\n", static_cast<unsigned long long>(sent));
  return sent > 0 ? 0 : 5;
}

int sub(const std::string & channel, uint32_t slot_size, int timeout_ms)
{
  auto ch = shm_transport::ShmChannel::open(channel);
  if (!ch) {
    std::printf("cpp sub: open failed\n");
    return 2;
  }
  std::vector<uint8_t> buf(slot_size);
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    size_t len = 0;
    if (ch->subscribe(buf.data(), buf.size(), &len, 500)) {
      buf.resize(len);
      nav_msgs::msg::Path msg;
      if (!deserialize(buf, msg)) {
        std::printf("cpp sub: deserialize failed\n");
        return 3;
      }
      if (!verify_canonical(msg)) {
        std::printf("cpp sub: content mismatch\n");
        return 4;
      }
      std::printf("cpp sub: OK (%zu bytes, %zu poses)\n", len, msg.poses.size());
      return 0;
    }
  }
  std::printf("cpp sub: timeout\n");
  return 6;
}

// The sizing rule, asked one name at a time. `python/interop_matrix.py` puts
// its own answer for the same names next to this one: the rule exists twice,
// in `shm_config.hpp` and in `ros2py/channel.py`, and the two copies have to
// agree on every name or a publisher and a subscriber open the same channel at
// two different layouts.
int sizes(int argc, char ** argv)
{
  for (int i = 2; i < argc; ++i) {
    const std::string topic = argv[i];
    std::printf("%s %u\n", topic.c_str(), rclcpp::shim::slot_size_for_topic(topic));
  }
  return 0;
}

}  // namespace

int main(int argc, char ** argv)
{
  if (argc < 3) {
    std::printf("usage: py_interop <dump|check|pub|sub> <arg> [args]\n");
    return 1;
  }
  const std::string mode = argv[1];
  if (mode == "dump") {
    return dump(argv[2]);
  }
  if (mode == "check") {
    return check(argv[2]);
  }
  if (mode == "pub") {
    return pub(argv[2], static_cast<uint32_t>(std::stoul(argv[3])));
  }
  if (mode == "sub") {
    return sub(argv[2], static_cast<uint32_t>(std::stoul(argv[3])), std::stoi(argv[4]));
  }
  if (mode == "sizes") {
    return sizes(argc, argv);
  }
  std::printf("unknown mode %s\n", mode.c_str());
  return 1;
}
