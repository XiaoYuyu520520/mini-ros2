// The bridge agent: the process the Windows daemon connects to.
//
// `ros2_grpc_sdk_mock_agent` is this program with `--backend mock` hardcoded -
// it serves the synthetic graph the localhost tests use. This one lets the
// backend be chosen at run time, which is what makes a *real* ROS2 peer
// possible:
//
//   ros2_grpc_sdk_agent 0.0.0.0:50051 --backend rclcpp
//
// A `--backend rclcpp` agent links against librclcpp and serves whatever graph
// this machine is actually running, so the Windows side of the bridge can talk
// to ROS2 instead of to `ros2_bridge_sim`. It is only built when the SDK was
// configured with -DROS2_GRPC_SDK_WITH_ROS2=ON (see rclcpp_backend.hpp); the
// flag is still accepted without it, and says so instead of failing to link.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "ros2_grpc_sdk/common/log.hpp"
#include "ros2_grpc_sdk/common/status.hpp"
#include "ros2_grpc_sdk/server/i_ros2_backend.hpp"
#include "ros2_grpc_sdk/server/mock_backend.hpp"
#include "ros2_grpc_sdk/server/ros2_bridge_server.hpp"

#if defined(ROS2_GRPC_SDK_WITH_ROS2)
#include "ros2_grpc_sdk/server/rclcpp_backend.hpp"
#endif

namespace {

std::atomic<bool> g_stop{false};

void handleSignal(int) { g_stop.store(true); }

void usage(const char* argv0) {
  std::cout << "usage: " << argv0
            << " [address] [--backend mock|rclcpp] [--node NAME]\n"
            << "  address   default 0.0.0.0:50051\n"
            << "  --backend default mock; rclcpp serves this machine's real "
               "ROS2 graph\n";
}

}  // namespace

int main(int argc, char** argv) {
  using namespace ros2_grpc_sdk;

  setLogLevel(LogLevel::Info);

  std::string address = "0.0.0.0:50051";
  std::string backend_name = "mock";
  std::string node_name = "ros2_grpc_bridge_agent";

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      usage(argv[0]);
      return 0;
    }
    if (arg == "--backend") {
      if (i + 1 >= argc) {
        std::cerr << "--backend needs a value" << std::endl;
        return 2;
      }
      backend_name = argv[++i];
    } else if (arg == "--node") {
      if (i + 1 >= argc) {
        std::cerr << "--node needs a value" << std::endl;
        return 2;
      }
      node_name = argv[++i];
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "unknown option: " << arg << std::endl;
      usage(argv[0]);
      return 2;
    } else {
      address = arg;
    }
  }

  IRos2Backend::Ptr backend;
  if (backend_name == "mock") {
    backend = std::make_shared<MockBackend>();
  } else if (backend_name == "rclcpp") {
#if defined(ROS2_GRPC_SDK_WITH_ROS2)
    RclcppBackendOptions options;
    options.node_name = node_name;
    backend = std::make_shared<RclcppBackend>(options);
#else
    std::cerr << "this build has no rclcpp backend: configure with "
                 "-DROS2_GRPC_SDK_WITH_ROS2=ON against a sourced ROS2 "
                 "environment"
              << std::endl;
    return 2;
#endif
  } else {
    std::cerr << "unknown backend: " << backend_name << std::endl;
    usage(argv[0]);
    return 2;
  }

  Ros2BridgeServer server(backend);
  const Status status = server.start(address);
  if (!status.ok()) {
    std::cerr << "failed to start agent: " << status.toString() << std::endl;
    return 1;
  }

  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  std::cout << "ros2 bridge agent listening on " << server.address()
            << " (backend=" << backend->name()
            << ", ros_distro=" << backend->rosDistro() << ")" << std::endl;
  std::cout << "press Ctrl+C to stop" << std::endl;

  while (!g_stop.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  server.stop();
  return 0;
}
