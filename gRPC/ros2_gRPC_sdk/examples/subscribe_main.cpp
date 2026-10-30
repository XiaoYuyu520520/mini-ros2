#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"
#include "ros2_grpc_sdk/common/log.hpp"

int main(int argc, char** argv) {
  using namespace ros2_grpc_sdk;

  const std::string target = argc > 1 ? argv[1] : "localhost:50051";
  const std::string topic = argc > 2 ? argv[2] : "/demo/chatter";
  const std::string type = argc > 3 ? argv[3] : "std_msgs/msg/String";
  const int seconds = argc > 4 ? std::atoi(argv[4]) : 5;

  Ros2BridgeClient client;
  Status status = client.connect(target);
  if (!status.ok()) {
    std::cerr << "connect failed: " << status.toString() << std::endl;
    return 1;
  }

  std::atomic<int> received{0};
  status = client.subscribe(
      topic, type,
      [&received](const Message& message) {
        const int index = received.fetch_add(1);
        std::cout << "[" << index << "] " << message.topic << " ("
                  << message.type << ") " << message.data.size() << " bytes"
                  << std::endl;
      },
      [](const Status& error) {
        std::cerr << "subscription error: " << error.toString() << std::endl;
      });
  if (!status.ok()) {
    std::cerr << "subscribe failed: " << status.toString() << std::endl;
    return 1;
  }

  std::cout << "subscribed to " << topic << " for " << seconds
            << "s (Ctrl+C to stop early)" << std::endl;
  for (int elapsed = 0; elapsed < seconds * 10; ++elapsed) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  client.unsubscribe(topic);
  std::cout << "received " << received.load() << " messages" << std::endl;
  return 0;
}
