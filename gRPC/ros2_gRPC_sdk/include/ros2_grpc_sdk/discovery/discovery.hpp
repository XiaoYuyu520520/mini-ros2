#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ros2_grpc_sdk/common/status.hpp"
#include "ros2_grpc_sdk/export.hpp"

namespace ros2_grpc_sdk {

// Default TCP port an agent listens on (see ServerOptions::address).
inline constexpr std::uint16_t kDefaultBridgePort = 50051;

// A local network interface with one IPv4 address and its subnet, mirroring
// the netscan/lanscan reference implementation.
struct ROS2_GRPC_SDK_API NetworkInterface {
  std::string name;
  std::string ip;
  std::string netmask;
  int prefix_length = 0;
  std::string cidr;  // e.g. "192.168.1.0/24"
  std::string mac;
  bool is_up = false;
  bool is_loopback = false;
};

struct ROS2_GRPC_SDK_API DiscoveryOptions {
  // TCP ports to probe on every candidate host.
  std::vector<std::uint16_t> ports{kDefaultBridgePort};
  // TCP connect timeout for a single (host, port) probe.
  int connect_timeout_ms = 150;
  // Maximum concurrent TCP probes.
  int concurrency = 256;
  // Cap on the number of addresses expanded from a single subnet.
  std::size_t max_hosts_per_subnet = 1024;
  // Confirm a TCP-open endpoint is a bridge agent with a gRPC Ping and use
  // the RPC round-trip as the latency metric.
  bool verify_with_ping = true;
  // Per-candidate gRPC Ping timeout.
  int ping_timeout_ms = 500;
  // Total wall-clock budget for a discovery run.
  int overall_timeout_ms = 3000;
  // Include the local machine's own addresses (default: excluded).
  bool include_self = false;
  // Include loopback interfaces when enumerating NICs (default: excluded).
  bool include_loopback = false;
  // Explicit subnets ("192.168.1.0/24") to scan. When empty, all non-loopback
  // interface subnets are scanned automatically.
  std::vector<std::string> subnets;
  // Maximum number of agents returned (after sorting by latency).
  std::size_t max_results = 32;
};

struct ROS2_GRPC_SDK_API DiscoveredAgent {
  std::string host;
  std::uint16_t port = 0;
  // Round-trip latency in milliseconds (gRPC Ping when verified, otherwise
  // the TCP connect time).
  double latency_ms = 0.0;
  // Local interface used to reach the agent (when known).
  std::string interface_name;
  std::string ros_distro;
  std::string agent_version;

  std::string target() const {
    return host + ":" + std::to_string(static_cast<unsigned>(port));
  }
};

// Enumerate local IPv4 interfaces. Loopback is skipped unless requested.
// Down interfaces are skipped unless `include_down` is set (the exclusion set
// uses include_down=true so addresses on temporarily disconnected adapters are
// still recognised as "this machine").
ROS2_GRPC_SDK_API std::vector<NetworkInterface> listNetworkInterfaces(
    bool include_loopback = false, bool include_down = false);

// All local IPv4 addresses (optionally including loopback / down interfaces).
ROS2_GRPC_SDK_API std::vector<std::string> localIPv4Addresses(
    bool include_loopback = false, bool include_down = false);

// True for any address in 127.0.0.0/8.
ROS2_GRPC_SDK_API bool isLoopbackAddress(const std::string& ip);

// Expand a CIDR into usable host addresses. Network/broadcast addresses are
// dropped for prefixes <= /30. `exclude` entries are removed. Returns an empty
// vector for malformed input or when the subnet exceeds `max_hosts`.
ROS2_GRPC_SDK_API std::vector<std::string> expandCidr(
    const std::string& cidr, std::size_t max_hosts = 1024,
    const std::vector<std::string>& exclude = {});

// Progress callback: (hosts_probed, hosts_total).
using DiscoveryProgress = std::function<void(std::size_t, std::size_t)>;

// Scans the LAN for reachable bridge agents. Fast path: concurrent TCP
// connects across every interface subnet, then a gRPC Ping to confirm and
// time the survivors. Results are sorted by ascending latency and the local
// machine / loopback are excluded by default.
class ROS2_GRPC_SDK_API Ros2BridgeDiscovery {
 public:
  explicit Ros2BridgeDiscovery(DiscoveryOptions options = {});

  const DiscoveryOptions& options() const { return options_; }

  // Interfaces that would be scanned with the current options.
  std::vector<NetworkInterface> interfaces() const;

  // Runs a discovery. `agents_out` is filled with the reachable agents sorted
  // by ascending latency.
  Status discover(std::vector<DiscoveredAgent>& agents_out,
                  DiscoveryProgress progress = nullptr);

  // Convenience wrapper returning only the lowest-latency agent.
  Status discoverBest(DiscoveredAgent& best_out,
                      DiscoveryProgress progress = nullptr);

  // A per-run elapsed-time budget is derived from options().overall_timeout_ms.

 private:
  DiscoveryOptions options_;
};

}  // namespace ros2_grpc_sdk
