// End-to-end demo that exercises the whole SDK against a local (or LAN) agent
// in a single process. It:
//   1. starts a mock agent (simulated ROS2 graph),
//   2. connects a client over gRPC,
//   3. lists topics,
//   4. subscribes to a live synthetic topic,
//   5. publishes a message and observes the loopback round trip.
//
// Run with:
//   ros2_grpc_sdk_demo [address]
// The default address is 127.0.0.1:50055.

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"
#include "ros2_grpc_sdk/common/log.hpp"
#include "ros2_grpc_sdk/common/ros2_types.hpp"
#include "ros2_grpc_sdk/server/mock_backend.hpp"
#include "ros2_grpc_sdk/server/ros2_bridge_server.hpp"

namespace {

bool waitFor(const std::function<bool()>& predicate, int timeout_ms) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return predicate();
}

}  // namespace

int main(int argc, char** argv) {
  using namespace ros2_grpc_sdk;

  setLogLevel(LogLevel::Info);
  const std::string address = argc > 1 ? argv[1] : "127.0.0.1:50055";

  auto backend = std::make_shared<MockBackend>();
  Ros2BridgeServer server(backend);
  Status status = server.start(address);
  if (!status.ok()) {
    std::cerr << "failed to start agent: " << status.toString() << std::endl;
    return 1;
  }

  Ros2BridgeClient client;
  status = client.connect(address);
  if (!status.ok()) {
    std::cerr << "connect failed: " << status.toString() << std::endl;
    server.stop();
    return 1;
  }

  std::string echo;
  status = client.ping("hello", echo);
  std::cout << "[1] ping -> " << status.toString() << " echo='" << echo << "'"
            << std::endl;

  std::vector<TopicInfo> topics;
  status = client.listTopics(topics);
  std::cout << "[2] listTopics -> " << status.toString() << " ("
            << topics.size() << " topics)" << std::endl;
  for (const auto& topic : topics) {
    std::cout << "      " << topic.name << " [" << topic.type << "]" << std::endl;
  }

  TopicInfo info;
  status = client.getTopicInfo("/demo/counter", info);
  std::cout << "[3] getTopicInfo(/demo/counter) -> " << status.toString()
            << " type=" << info.type << std::endl;

  std::atomic<int> live_count{0};
  status = client.subscribe(
      "/demo/chatter", "std_msgs/msg/String",
      [&live_count](const Message& message) {
        const int index = live_count.fetch_add(1);
        if (index < 3) {
          std::cout << "      live message #" << index << ": '"
                    << ros2::decodeString(message.data) << "'" << std::endl;
        }
      });
  std::cout << "[4] subscribe(/demo/chatter) -> " << status.toString()
            << std::endl;

  std::atomic<int> echoed{0};
  std::string echoed_payload;
  status = client.subscribe(
      "/demo/echo", "std_msgs/msg/String",
      [&echoed, &echoed_payload](const Message& message) {
        echoed_payload = ros2::decodeString(message.data);
        echoed.fetch_add(1);
      });
  std::cout << "[5] subscribe(/demo/echo) -> " << status.toString() << std::endl;

  const std::string outbound = "round-trip from windows client";
  status = client.publish("/demo/echo", "std_msgs/msg/String",
                          ros2::encodeString(outbound));
  std::cout << "[6] publish(/demo/echo) -> " << status.toString() << std::endl;

  // The streaming subscription is established asynchronously on the agent, so
  // keep publishing until the loopback is observed (like a real publisher).
  for (int attempt = 0; attempt < 40 && echoed.load() == 0; ++attempt) {
    client.publish("/demo/echo", "std_msgs/msg/String",
                   ros2::encodeString(outbound));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  const bool got_echo = waitFor([&echoed]() { return echoed.load() > 0; }, 2000);
  std::cout << "[7] loopback round trip -> "
            << (got_echo ? "OK" : "TIMEOUT");
  if (got_echo) {
    std::cout << " payload='" << echoed_payload << "'";
  }
  std::cout << std::endl;

  const bool got_live = waitFor([&live_count]() { return live_count.load() > 0; }, 2000);
  std::cout << "[8] live synthetic publisher -> "
            << (got_live ? "OK" : "TIMEOUT") << " (" << live_count.load()
            << " messages)" << std::endl;

  client.unsubscribeAll();
  client.disconnect();
  server.stop();

  const bool ok = got_echo && got_live && info.type == "example_interfaces/msg/Int64";
  std::cout << (ok ? "DEMO PASSED" : "DEMO FAILED") << std::endl;
  return ok ? 0 : 1;
}
