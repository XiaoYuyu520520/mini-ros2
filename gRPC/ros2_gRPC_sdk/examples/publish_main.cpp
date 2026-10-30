#include <cstdlib>
#include <iostream>
#include <string>

#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"
#include "ros2_grpc_sdk/common/log.hpp"
#include "ros2_grpc_sdk/common/ros2_types.hpp"

int main(int argc, char** argv) {
  using namespace ros2_grpc_sdk;

  const std::string target = argc > 1 ? argv[1] : "localhost:50051";
  const std::string topic = argc > 2 ? argv[2] : "/demo/echo";
  const std::string type = argc > 3 ? argv[3] : "std_msgs/msg/String";
  const std::string value = argc > 4 ? argv[4] : "hello from client";

  Ros2BridgeClient client;
  Status status = client.connect(target);
  if (!status.ok()) {
    std::cerr << "connect failed: " << status.toString() << std::endl;
    return 1;
  }

  Bytes payload;
  if (type.find("Int64") != std::string::npos) {
    payload = ros2::encodeInt64(std::atoll(value.c_str()));
  } else {
    payload = ros2::encodeString(value);
  }

  status = client.publish(topic, type, payload, "cdr");
  if (!status.ok()) {
    std::cerr << "publish failed: " << status.toString() << std::endl;
    return 1;
  }

  std::cout << "published " << payload.size() << " bytes to " << topic
            << " (type=" << type << ")" << std::endl;
  return 0;
}
