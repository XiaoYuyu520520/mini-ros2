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
  // Transport security. The agent is plaintext by default, which is fine for a
  // trusted lab LAN and wrong for anything else: `Publish` on a control topic
  // moves a robot, and a plaintext port can be driven by anyone who can reach
  // it. Set `tls_cert_chain_pem` and `tls_private_key_pem` (PEM contents, not
  // paths) to serve TLS. Leave `tls_client_ca_pem` empty for server-only TLS,
  // or set it to require a client certificate (mutual TLS).
  std::string tls_cert_chain_pem;
  std::string tls_private_key_pem;
  std::string tls_client_ca_pem;

  bool use_tls() const {
    return !tls_cert_chain_pem.empty() && !tls_private_key_pem.empty();
  }
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
