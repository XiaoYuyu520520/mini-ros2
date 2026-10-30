#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "ros2_grpc_sdk/common/log.hpp"
#include "ros2_grpc_sdk/server/mock_backend.hpp"
#include "ros2_grpc_sdk/server/ros2_bridge_server.hpp"

namespace {

std::atomic<bool> g_stop{false};

void handleSignal(int) { g_stop.store(true); }

}  // namespace

int main(int argc, char** argv) {
  using namespace ros2_grpc_sdk;

  setLogLevel(LogLevel::Info);

  const std::string address = argc > 1 ? argv[1] : "0.0.0.0:50051";

  auto backend = std::make_shared<MockBackend>();
  Ros2BridgeServer server(backend);
  const Status status = server.start(address);
  if (!status.ok()) {
    std::cerr << "failed to start agent: " << status.toString() << std::endl;
    return 1;
  }

  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  std::cout << "mock ros2 bridge agent listening on " << server.address()
            << " (backend=" << backend->name() << ")" << std::endl;
  std::cout << "press Ctrl+C to stop" << std::endl;

  while (!g_stop.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  server.stop();
  return 0;
}
