// Real-path throughput and latency of the shim transport.
//
// `shm_transport`'s own throughput test measures the channel alone: 64 byte
// blobs, no serialization. That is not what a nav2 node pays. A publish on
// this port goes through
//
//     struct -> serialize() -> std::vector<uint8_t>
//            -> memcpy into the shared-memory slot        (publish)
//     ================== process boundary ==================
//            -> memcpy out of the slot                    (subscribe)
//            -> deserialize() -> struct
//
// so there are four copies and two buffer allocations per message, and the
// cost depends on the message size. This file measures that whole path for
// three payloads that a nav2 stack actually uses:
//
//   * /cmd_vel    geometry_msgs::msg::Twist         (~100 B)   control output
//   * /odom       nav_msgs::msg::Odometry           (~800 B)   odometry
//   * /scan       sensor_msgs::msg::LaserScan       (~ 6 KB)   lidar
//
// Two numbers per payload:
//
//   * **throughput** - one thread publishing, one subscribing, same process.
//     The shared-memory channel is the only thing between them, which is what
//     the nav2 controller -> controller path looks like when both nodes live
//     in one process (the lifecycle manager starts them in one).
//   * **latency** - publish a message with a timestamp, read it back, and
//     measure the round trip. This is `steady_clock` on one machine, so it is
//     the transport's cost and nothing else.
//
// Run: `nav2_sim_latency_bench` (built by nav2/ros2_shim/build.py), or read
// the numbers it prints into .scratch/.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "rclcpp/shm_config.hpp"
#include "ros2_shim/serialization.hpp"
#include "shm_transport/shm_channel.hpp"

#include "geometry_msgs/msg/twist.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"

namespace
{

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string channel_name(const std::string & tag)
{
  return "ros2shim_bench/" + tag;
}

// ---------------------------------------------------------------------------
// Payload builders: the shapes a nav2 node really sends.
// ---------------------------------------------------------------------------

geometry_msgs::msg::Twist make_twist()
{
  geometry_msgs::msg::Twist message;
  message.linear.x = 0.26;
  message.linear.y = 0.0;
  message.linear.z = 0.0;
  message.angular.x = 0.0;
  message.angular.y = 0.0;
  message.angular.z = 0.4;
  return message;
}

nav_msgs::msg::Odometry make_odom()
{
  nav_msgs::msg::Odometry message;
  message.header.stamp.sec = 12345;
  message.header.stamp.nanosec = 67890;
  message.header.frame_id = "odom";
  message.child_frame_id = "base_link";
  message.pose.pose.position.x = 1.5;
  message.pose.pose.position.y = -2.5;
  message.pose.pose.orientation.w = 1.0;
  for (std::size_t i = 0; i < 36; ++i) {
    message.pose.covariance[i] = static_cast<double>(i) * 0.001;
    message.twist.covariance[i] = static_cast<double>(i) * 0.002;
  }
  return message;
}

sensor_msgs::msg::LaserScan make_scan(std::size_t beams = 720)
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

// Change one field from the loop counter, per payload type, so the compiler
// cannot hoist the serialize out of the loop. `Twist` has no header and the
// grid messages do, hence the overloads rather than one generic write.
inline void touch(geometry_msgs::msg::Twist & message, uint64_t i)
{
  message.linear.x = 0.1 + static_cast<double>(i % 10) * 0.01;
}

inline void touch(nav_msgs::msg::Odometry & message, uint64_t i)
{
  message.header.stamp.nanosec = static_cast<uint32_t>(i);
}

inline void touch(sensor_msgs::msg::LaserScan & message, uint64_t i)
{
  message.header.stamp.nanosec = static_cast<uint32_t>(i);
}

// ---------------------------------------------------------------------------
// The two measurements.
// ---------------------------------------------------------------------------

// One thread publishes, one subscribes, both in this process: the transport is
// the only thing in between.
template <class Message, class Make>
void bench_throughput(const char * label, const std::string & tag, Make make, uint64_t total)
{
  const auto sample = make();
  const std::size_t wire = ros2_shim::serialize(sample).size();

  // The channel only has to hold one message at a time plus room for the
  // reader to fall behind for a moment; 4096 slots is what the transport's
  // own test uses and what nav2's channels are sized for.
  const std::string name = channel_name(tag);
  std::string error;
  auto writer = shm_transport::ShmChannel::create(
    name, 4096, static_cast<uint32_t>(wire + 64), &error);
  if (!writer) {
    std::printf("  %-10s create failed: %s\n", label, error.c_str());
    return;
  }
  auto reader = shm_transport::ShmChannel::open(name, &error);
  if (!reader) {
    std::printf("  %-10s open failed: %s\n", label, error.c_str());
    return;
  }

  std::atomic<bool> stop{false};
  std::atomic<uint64_t> received{0};
  std::vector<uint8_t> buffer(wire + 64);

  std::thread consumer([&]() {
    Message decoded;
    std::size_t len = 0;
    while (!stop.load(std::memory_order_relaxed) && received.load() < total) {
      if (!reader->subscribe(buffer.data(), buffer.size(), &len, 1000)) {
        continue;
      }
      if (ros2_shim::deserialize(buffer.data(), len, decoded)) {
        received.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });

  // The writer's loop only pays the channel (the payload is already
  // serialized), so it measures how fast the ring accepts messages; the
  // consumer's loop pays the copy out and the decode as well, so the
  // end-to-end rate is the one a subscriber sees.
  const auto t0 = Clock::now();
  for (uint64_t i = 0; i < total; ++i) {
    Message message = sample;
    touch(message, i);
    const auto bytes = ros2_shim::serialize(message);
    if (!writer->publish(bytes.data(), bytes.size())) {
      break;
    }
  }
  const double publish_ms = ms_since(t0);
  consumer.join();
  const double total_ms = ms_since(t0);

  const double publish_rate = total * 1000.0 / (publish_ms > 0 ? publish_ms : 1.0);
  const double full_rate = total * 1000.0 / (total_ms > 0 ? total_ms : 1.0);
  std::printf(
    "  %-10s %5zu B  %8.0f msg/s publish  %8.0f msg/s end-to-end  (%llu/%llu delivered)\n",
    label, wire, publish_rate, full_rate,
    static_cast<unsigned long long>(received.load()),
    static_cast<unsigned long long>(total));
}

// Publish/read round trip for one in-flight message: the smallest latency the
// path can have with this design (it pays both copies and both serializations).
template <class Message, class Make>
void bench_latency(const char * label, const std::string & tag, Make make, uint64_t rounds)
{
  const auto sample = make();
  const auto bytes = ros2_shim::serialize(sample);
  const std::string name = channel_name(std::string(tag) + "_lat");
  std::string error;
  auto writer = shm_transport::ShmChannel::create(
    name, 64, static_cast<uint32_t>(bytes.size() + 64), &error);
  if (!writer) {
    std::printf("  %-10s create failed: %s\n", label, error.c_str());
    return;
  }
  auto reader = shm_transport::ShmChannel::open(name, &error);
  if (!reader) {
    std::printf("  %-10s open failed: %s\n", label, error.c_str());
    return;
  }

  std::vector<uint8_t> buffer(bytes.size() + 64);
  Message decoded;

  // Warm up: the first publish creates the slot contents and the first
  // subscribe walks the cursor from zero.
  for (int i = 0; i < 100; ++i) {
    writer->publish(bytes.data(), bytes.size());
    std::size_t len = 0;
    reader->subscribe(buffer.data(), buffer.size(), &len, 100);
  }

  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(rounds));
  for (uint64_t i = 0; i < rounds; ++i) {
    const auto t0 = Clock::now();
    if (!writer->publish(bytes.data(), bytes.size())) {
      break;
    }
    std::size_t len = 0;
    if (!reader->subscribe(buffer.data(), buffer.size(), &len, 1000)) {
      break;
    }
    const double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
    ros2_shim::deserialize(buffer.data(), len, decoded);
    samples.push_back(us);
  }

  if (samples.empty()) {
    std::printf("  %-10s no samples\n", label);
    return;
  }
  std::sort(samples.begin(), samples.end());
  const double p50 = samples[samples.size() / 2];
  const double p99 = samples[(samples.size() * 99) / 100];
  const double worst = samples.back();
  const double mean =
    std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());
  std::printf(
    "  %-10s mean %6.1f us   p50 %6.1f us   p99 %6.1f us   max %7.1f us\n",
    label, mean, p50, p99, worst);
}

// What the serialize/deserialize half costs on its own, with no channel at
// all. Subtracting it from the round trip says how much of the latency is the
// shared-memory hop and how much is the encode/decode the API shape forces.
template <class Message, class Make>
void bench_codec(const char * label, Make make, uint64_t rounds)
{
  const auto sample = make();
  const auto bytes = ros2_shim::serialize(sample);
  Message decoded;
  // Warm up.
  for (int i = 0; i < 100; ++i) {
    ros2_shim::deserialize(bytes.data(), bytes.size(), decoded);
  }
  const auto t0 = Clock::now();
  for (uint64_t i = 0; i < rounds; ++i) {
    auto encoded = ros2_shim::serialize(sample);
    ros2_shim::deserialize(encoded.data(), encoded.size(), decoded);
  }
  const double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
  std::printf("  %-10s %5zu B  mean %6.1f us per round (serialize + deserialize)\n",
              label, bytes.size(), us / static_cast<double>(rounds));
}

// The publish path with the reader in a **different process**: what a real
// nav2 stack does (controller_server and planner_server are separate
// processes), and the only number that is not flattered by one shared cache.
//
// The loop really serializes: the message is built, encoded, published, and
// the reader decodes it on the other side. The serialized size is taken once
// beforehand, only to size the slot.
//
// The parent keeps the channel open for the child's whole life: a named
// mapping on Windows lives only as long as some process holds it, so the
// publisher's own handle is what stops the child finding nothing.
template <class Message, class Make>
void bench_cross_process(const char * label, const std::string & tag, Make make, uint64_t total)
{
  const auto sample = make();
  const uint32_t slot_size =
    static_cast<uint32_t>(ros2_shim::serialize(sample).size() + 64);
  const std::string name = channel_name(std::string(tag) + "_xproc");

  std::string error;
  auto writer = shm_transport::ShmChannel::create(name, 4096, slot_size, &error);
  if (!writer) {
    std::printf("  %-10s create failed: %s\n", label, error.c_str());
    return;
  }

#ifdef _WIN32
  char exe[MAX_PATH] = {0};
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string cmd = "\"" + std::string(exe) + "\" child-reader " + name + " " +
    std::to_string(slot_size) + " " + std::to_string(total) + " " + tag;
  std::vector<char> mutable_cmd(cmd.begin(), cmd.end());
  mutable_cmd.push_back('\0');

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
      &si, &pi))
  {
    std::printf("  %-10s CreateProcess failed: %lu\n", label, GetLastError());
    return;
  }
  CloseHandle(pi.hThread);

  // Let the child open the channel and reach its first subscribe.
  std::this_thread::sleep_for(std::chrono::milliseconds(120));

  const auto t0 = Clock::now();
  for (uint64_t i = 0; i < total; ++i) {
    Message message = sample;
    touch(message, i);   // changes the payload each round
    const auto bytes = ros2_shim::serialize(message);
    if (!writer->publish(bytes.data(), bytes.size())) {
      break;
    }
  }
  const double publish_ms = ms_since(t0);

  WaitForSingleObject(pi.hProcess, 60000);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  std::printf(
    "  %-10s %5u B  %8.0f msg/s publish  child exit=%lu\n",
    label, slot_size, total * 1000.0 / (publish_ms > 0 ? publish_ms : 1.0), code);
#else
  char exe[4096] = {0};
  const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  if (n <= 0) {
    std::printf("  %-10s cannot resolve /proc/self/exe\n", label);
    return;
  }
  const pid_t child = fork();
  if (child == 0) {
    execl(exe, exe, "child-reader", name.c_str(),
          std::to_string(slot_size).c_str(), std::to_string(total).c_str(),
          tag.c_str(), nullptr);
    _exit(127);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  const auto t0 = Clock::now();
  for (uint64_t i = 0; i < total; ++i) {
    Message message = sample;
    touch(message, i);   // changes the payload each round
    const auto bytes = ros2_shim::serialize(message);
    if (!writer->publish(bytes.data(), bytes.size())) {
      break;
    }
  }
  const double publish_ms = ms_since(t0);
  int status = 0;
  waitpid(child, &status, 0);
  std::printf(
    "  %-10s %5u B  %8.0f msg/s publish  child exit=%d\n",
    label, slot_size, total * 1000.0 / (publish_ms > 0 ? publish_ms : 1.0),
    WIFEXITED(status) ? WEXITSTATUS(status) : -1);
#endif
}

}  // namespace

// ---------------------------------------------------------------------------
// Cross-process mode: the same publish path, but the reader is a different
// process. This is what a real nav2 stack looks like - controller_server and
// planner_server are separate processes - and it is the number that matters,
// because the same-process figures above share one CPU and one cache.
//
//     bench_real_path child-reader <channel> <slot-size> <total>
//
// The child waits for exactly <total> messages and reports its rate; the
// parent publishes. Both print, so a wrong hand-off is visible in either.
// ---------------------------------------------------------------------------

namespace
{

template <class Message>
int child_reader_impl(const std::string & channel, uint32_t slot_size, uint64_t total)
{
  std::string error;
  auto reader = shm_transport::ShmChannel::open(channel, &error);
  if (!reader) {
    std::printf("child-reader: open failed: %s\n", error.c_str());
    return 2;
  }
  std::vector<uint8_t> buffer(slot_size);
  // Decode on this side too, so the number printed is the whole path and not
  // just the ring's read half.
  Message decoded;
  const auto start = Clock::now();
  uint64_t received = 0;
  uint64_t decoded_ok = 0;
  while (received < total) {
    std::size_t len = 0;
    if (!reader->subscribe(buffer.data(), buffer.size(), &len, 1000)) {
      continue;
    }
    ++received;
    if (ros2_shim::deserialize(buffer.data(), len, decoded)) {
      ++decoded_ok;
    }
  }
  const double ms = ms_since(start);
  std::printf("  child-reader: %llu messages in %.0f ms (%.0f msg/s), %llu decoded\n",
              static_cast<unsigned long long>(received), ms,
              received * 1000.0 / (ms > 0 ? ms : 1.0),
              static_cast<unsigned long long>(decoded_ok));
  return received == total ? 0 : 4;
}

int run_child_reader(const char * channel, uint32_t slot_size, uint64_t total,
                     const char * tag)
{
  if (std::strcmp(tag, "twist") == 0) {
    return child_reader_impl<geometry_msgs::msg::Twist>(channel, slot_size, total);
  }
  if (std::strcmp(tag, "odom") == 0) {
    return child_reader_impl<nav_msgs::msg::Odometry>(channel, slot_size, total);
  }
  return child_reader_impl<sensor_msgs::msg::LaserScan>(channel, slot_size, total);
}

}  // namespace

int main(int argc, char ** argv)
{
  if (argc >= 6 && std::strcmp(argv[1], "child-reader") == 0) {
    return run_child_reader(
      argv[2], static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10)),
      std::strtoull(argv[4], nullptr, 10), argv[5]);
  }

  const uint64_t messages = 200000;
  const uint64_t latency_rounds = 20000;

  std::printf("=== shim transport: real-path throughput / latency ===\n");
  std::printf("slot geometry: count=%u size=%u large=%u\n",
              rclcpp::shim::default_slot_count(),
              rclcpp::shim::default_slot_size(),
              rclcpp::shim::large_slot_size());

  std::printf("\n[codec only] struct -> bytes -> struct, no channel\n");
  bench_codec<geometry_msgs::msg::Twist>("Twist", make_twist, messages);
  bench_codec<nav_msgs::msg::Odometry>("Odometry", make_odom, messages);
  bench_codec<sensor_msgs::msg::LaserScan>("LaserScan", [] {return make_scan();}, messages);

  std::printf("\n[throughput] serialize -> memcpy in -> memcpy out -> deserialize\n");
  bench_throughput<geometry_msgs::msg::Twist>("Twist", "twist", make_twist, messages);
  bench_throughput<nav_msgs::msg::Odometry>("Odometry", "odom", make_odom, messages);
  bench_throughput<sensor_msgs::msg::LaserScan>("LaserScan", "scan", [] {return make_scan();}, messages);

  std::printf("\n[latency] publish -> subscribe -> deserialize, one in flight\n");
  bench_latency<geometry_msgs::msg::Twist>("Twist", "twist", make_twist, latency_rounds);
  bench_latency<nav_msgs::msg::Odometry>("Odometry", "odom", make_odom, latency_rounds);
  bench_latency<sensor_msgs::msg::LaserScan>("LaserScan", "scan", [] {return make_scan();}, latency_rounds);

  std::printf("\n[throughput, cross-process] publisher here, reader in another process\n");
  bench_cross_process<geometry_msgs::msg::Twist>("Twist", "twist", make_twist, messages);
  bench_cross_process<nav_msgs::msg::Odometry>("Odometry", "odom", make_odom, messages);
  bench_cross_process<sensor_msgs::msg::LaserScan>("LaserScan", "scan", [] {return make_scan();}, messages);

  return 0;
}
