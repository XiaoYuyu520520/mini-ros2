#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"
#include "ros2_grpc_sdk/common/log.hpp"

int main(int argc, char** argv) {
  using namespace ros2_grpc_sdk;

  const std::string target = argc > 1 ? argv[1] : "localhost:50051";
  const std::string filter = argc > 2 ? argv[2] : "";

  Ros2BridgeClient client;
  Status status = client.connect(target);
  if (!status.ok()) {
    std::cerr << "connect failed: " << status.toString() << std::endl;
    return 1;
  }

  std::vector<TopicInfo> topics;
  status = client.listTopics(topics, filter);
  if (!status.ok()) {
    std::cerr << "listTopics failed: " << status.toString() << std::endl;
    return 1;
  }

  std::cout << "topics on " << target << " (" << topics.size() << ")";
  if (!filter.empty()) {
    std::cout << " matching '" << filter << "'";
  }
  std::cout << ":\n";
  for (const auto& topic : topics) {
    std::cout << "  " << std::left << std::setw(24) << topic.name << " "
              << std::setw(32) << topic.type << " pub=" << topic.publisher_count
              << " sub=" << topic.subscriber_count << "\n";
  }
  return 0;
}
