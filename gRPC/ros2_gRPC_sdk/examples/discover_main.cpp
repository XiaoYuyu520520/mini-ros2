// LAN discovery demo. Scans every local network interface for ros2 bridge
// agents, prints them ordered by latency and connects the client to the
// nearest one.
//
//   ros2_grpc_sdk_discover [ports] [timeout_ms] [subnet] [--self] [--config FILE]
//     ports      comma separated TCP ports (default: from config)
//     timeout_ms overall discovery budget (default: from config)
//     subnet     optional explicit CIDR (default: every local subnet)
//     --self     include this machine / loopback (handy to verify locally)
//     --config   explicit config file (default: the config folder)

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"
#include "ros2_grpc_sdk/common/config.hpp"
#include "ros2_grpc_sdk/common/log.hpp"
#include "ros2_grpc_sdk/discovery/discovery.hpp"

namespace {

std::vector<std::uint16_t> parsePorts(const std::string& text) {
  std::vector<std::uint16_t> ports;
  std::stringstream stream(text);
  std::string item;
  while (std::getline(stream, item, ',')) {
    if (item.empty()) {
      continue;
    }
    const int port = std::atoi(item.c_str());
    if (port > 0 && port <= 65535) {
      ports.push_back(static_cast<std::uint16_t>(port));
    }
  }
  return ports;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace ros2_grpc_sdk;

  setLogLevel(LogLevel::Info);

  std::string config_path;
  bool force_self = false;
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--self") {
      force_self = true;
    } else if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    } else {
      positional.push_back(arg);
    }
  }

  Config config;
  const Status config_status = loadConfig(config, config_path);
  if (!config_status.ok()) {
    std::cerr << "config error: " << config_status.toString() << std::endl;
    return 1;
  }
  std::cout << "config: "
            << (config_path.empty() ? configFilePath() : config_path) << "\n";

  DiscoveryOptions options = toDiscoveryOptions(config.discovery);
  if (force_self) {
    options.include_self = true;
    options.include_loopback = true;
  }
  if (positional.size() > 0) {
    const auto ports = parsePorts(positional[0]);
    if (!ports.empty()) {
      options.ports = ports;
    }
  }
  if (positional.size() > 1) {
    options.overall_timeout_ms = std::atoi(positional[1].c_str());
  }
  if (positional.size() > 2) {
    options.subnets.push_back(positional[2]);
  }

  Ros2BridgeDiscovery discovery(options);

  std::cout << "network interfaces:\n";
  for (const auto& net : discovery.interfaces()) {
    std::cout << "  " << net.name << "  " << net.ip << "  " << net.cidr;
    if (!net.mac.empty()) {
      std::cout << "  " << net.mac;
    }
    std::cout << "\n";
  }

  std::cout << "probing ports:";
  for (std::uint16_t port : options.ports) {
    std::cout << " " << port;
  }
  std::cout << "\n";

  std::vector<DiscoveredAgent> agents;
  const Status status = discovery.discover(
      agents, [](std::size_t probed, std::size_t total) {
        std::cout << "\r  probing " << probed << "/" << total << std::flush;
      });
  std::cout << "\n";
  if (!status.ok()) {
    std::cerr << "discovery failed: " << status.toString() << std::endl;
    return 1;
  }

  std::cout << "found " << agents.size() << " agent(s):\n";
  for (const auto& agent : agents) {
    std::cout << "  " << agent.target() << "  " << agent.latency_ms << " ms";
    if (!agent.ros_distro.empty()) {
      std::cout << "  distro=" << agent.ros_distro;
    }
    if (!agent.agent_version.empty()) {
      std::cout << "  sdk=" << agent.agent_version;
    }
    std::cout << "\n";
  }
  if (agents.empty()) {
    std::cout << "no bridge agent found" << std::endl;
    return 1;
  }

  Ros2BridgeClient client;
  DiscoveredAgent chosen;
  const Status connect_status = client.connectToNearestAgent(options, &chosen);
  if (!connect_status.ok()) {
    std::cerr << "connect failed: " << connect_status.toString() << std::endl;
    return 1;
  }

  std::cout << "connected to nearest agent " << chosen.target() << " ("
            << chosen.latency_ms << " ms)\n";

  std::vector<TopicInfo> topics;
  if (client.listTopics(topics).ok()) {
    std::cout << "topics on " << chosen.target() << " (" << topics.size()
              << "):\n";
    for (const auto& topic : topics) {
      std::cout << "  " << topic.name << "  [" << topic.type << "]\n";
    }
  }
  return 0;
}
