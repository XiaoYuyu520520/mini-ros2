#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"
#include "ros2_grpc_sdk/common/status.hpp"
#include "ros2_grpc_sdk/discovery/discovery.hpp"
#include "ros2_grpc_sdk/export.hpp"
#include "ros2_grpc_sdk/server/ros2_bridge_server.hpp"

namespace ros2_grpc_sdk {

// On-disk configuration. Mirrors the reference project's config.json /
// lanscan-options.json layout, but stored as YAML so the file can carry
// comments. Sections: server, client, discovery.
struct ROS2_GRPC_SDK_API DiscoveryConfig {
  std::vector<std::uint16_t> ports{kDefaultBridgePort};
  int connect_timeout_ms = 150;
  int concurrency = 256;
  std::size_t max_hosts_per_subnet = 1024;
  bool verify_with_ping = true;
  int ping_timeout_ms = 500;
  int overall_timeout_ms = 3000;
  bool include_self = false;
  bool include_loopback = false;
  std::vector<std::string> subnets;
  std::size_t max_results = 32;
};

struct ROS2_GRPC_SDK_API ClientConfig {
  int connect_timeout_ms = 3000;
  int rpc_timeout_ms = 5000;
  bool use_tls = false;
  std::string tls_root_cert_pem;
  std::string tls_target_name_override;
};

struct ROS2_GRPC_SDK_API ServerConfig {
  std::string address = "0.0.0.0:50051";
  int num_threads = 0;
};

struct ROS2_GRPC_SDK_API Config {
  DiscoveryConfig discovery;
  ClientConfig client;
  ServerConfig server;
};

// --- Config folder resolution ---------------------------------------------
//
// Search order:
//   1. ROS2_GRPC_SDK_CONFIG_DIR environment variable (a directory)
//   2. ./config if it exists (the folder shipped in the repository)
//   3. per-user location: %APPDATA%/ros2_gRPC_sdk (Windows) or
//      $XDG_CONFIG_HOME/ros2_gRPC_sdk / ~/.config/ros2_gRPC_sdk (Linux/macOS)
//
// configFilePath() just joins the resolved directory with a file name; the
// directory is created lazily by saveConfig().
ROS2_GRPC_SDK_API std::string configDirectory();
ROS2_GRPC_SDK_API std::string configFilePath(
    const std::string& name = "ros2_gRPC_sdk.yaml");

// Loads config from `path` (default resolution when empty). A missing file is
// not an error: the built-in defaults are kept. YAML comments are ignored.
// Parse/IO errors are reported.
ROS2_GRPC_SDK_API Status loadConfig(Config& out, const std::string& path = {});

// Writes `config` as a commented YAML document to `path` (default resolution
// when empty), creating the configuration directory when needed. Note: this
// regenerates the file, so pass in a different path to preserve a file whose
// comments you want to keep.
ROS2_GRPC_SDK_API Status saveConfig(const Config& config,
                                    const std::string& path = {});

// --- Mapping to runtime option structs -------------------------------------
ROS2_GRPC_SDK_API DiscoveryOptions toDiscoveryOptions(const DiscoveryConfig&);
ROS2_GRPC_SDK_API ClientOptions toClientOptions(const ClientConfig&);
ROS2_GRPC_SDK_API ServerOptions toServerOptions(const ServerConfig&);

}  // namespace ros2_grpc_sdk
