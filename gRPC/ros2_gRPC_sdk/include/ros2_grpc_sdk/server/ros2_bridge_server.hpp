#pragma once

#include <memory>
#include <string>

#include "ros2_grpc_sdk/common/status.hpp"
#include "ros2_grpc_sdk/server/i_ros2_backend.hpp"

namespace ros2bridge {
class Ros2Bridge;
}  // namespace ros2bridge

namespace ros2_grpc_sdk {

struct ServerOptions {
  // Interface/port to bind, e.g. "0.0.0.0:50051". Binding to 0.0.0.0 makes
  // the agent reachable from the whole LAN.
  std::string address = "0.0.0.0:50051";
  // Number of gRPC worker threads. 0 lets gRPC pick a default.
  int num_threads = 0;
  // Maximum number of concurrently streaming subscribers.
  int max_concurrent_streams = 256;
};

// Runs a gRPC service on top of an `IRos2Backend`.
//
//   auto backend = std::make_shared<MockBackend>();
//   Ros2BridgeServer server(backend);
//   server.start("0.0.0.0:50051");
//   server.wait();
//
// `start()` returns once the port is bound; `wait()` blocks until `stop()`.
class ROS2_GRPC_SDK_API Ros2BridgeServer {
 public:
  explicit Ros2BridgeServer(IRos2Backend::Ptr backend,
                            ServerOptions options = {});
  ~Ros2BridgeServer();

  Ros2BridgeServer(const Ros2BridgeServer&) = delete;
  Ros2BridgeServer& operator=(const Ros2BridgeServer&) = delete;

  Status start();
  Status start(const std::string& address);
  // Gracefully drains in-flight RPCs and shuts the gRPC server down.
  void stop();
  // Blocks until the server has been stopped.
  void wait();

  bool running() const;
  const std::string& address() const;
  IRos2Backend::Ptr backend() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ros2_grpc_sdk
