// Config folder + self-address handling tests. Verifies the YAML config
// round-trip, default resolution and the completeness of local-address
// exclusion helpers.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "ros2_grpc_sdk/common/config.hpp"
#include "ros2_grpc_sdk/common/log.hpp"
#include "ros2_grpc_sdk/discovery/discovery.hpp"

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
  if (condition) {
    std::cout << "  [ OK ] " << what << std::endl;
  } else {
    ++g_failures;
    std::cout << "  [FAIL] " << what << std::endl;
  }
}

bool contains(const std::vector<std::string>& items, const std::string& value) {
  for (const auto& item : items) {
    if (item == value) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  using namespace ros2_grpc_sdk;
  setLogLevel(LogLevel::Warn);

  const std::string path = "config_test_tmp.yaml";

  std::cout << "== config folder resolution ==" << std::endl;
  check(!configDirectory().empty(), "configDirectory() is non-empty");
  check(configFilePath().find("ros2_gRPC_sdk.yaml") != std::string::npos,
        "configFilePath() points at ros2_gRPC_sdk.yaml");

  std::cout << "== defaults when the file is missing ==" << std::endl;
  Config missing;
  Status status = loadConfig(missing, "definitely_missing_config_xyz.yaml");
  check(status.ok(), "loadConfig(missing) is ok");
  check(missing.discovery.ports.size() == 1 &&
            missing.discovery.ports[0] == kDefaultBridgePort,
        "default port is kDefaultBridgePort");

  std::cout << "== round trip ==" << std::endl;
  Config config;
  config.discovery.ports = {50051, 50052, 60000};
  config.discovery.include_self = true;
  config.discovery.subnets = {"10.0.0.0/24", "192.168.5.0/24"};
  config.discovery.overall_timeout_ms = 4321;
  config.server.address = "0.0.0.0:60000";
  config.server.num_threads = 4;
  config.client.rpc_timeout_ms = 1234;

  status = saveConfig(config, path);
  check(status.ok(), "saveConfig(): " + status.toString());

  Config loaded;
  status = loadConfig(loaded, path);
  check(status.ok(), "loadConfig(): " + status.toString());
  check(loaded.discovery.ports == config.discovery.ports, "ports round-trip");
  check(loaded.discovery.include_self, "include_self round-trip");
  check(loaded.discovery.subnets == config.discovery.subnets,
        "subnets round-trip");
  check(loaded.discovery.overall_timeout_ms == 4321, "timeout round-trip");
  check(loaded.server.address == "0.0.0.0:60000", "server address round-trip");
  check(loaded.server.num_threads == 4, "server threads round-trip");
  check(loaded.client.rpc_timeout_ms == 1234, "client timeout round-trip");

  DiscoveryOptions options = toDiscoveryOptions(loaded.discovery);
  check(options.ports.size() == 3 && options.include_self,
        "toDiscoveryOptions() maps fields");

  std::cout << "== YAML comments are ignored ==" << std::endl;
  {
    std::ofstream file("config_test_comments.yaml",
                       std::ios::binary | std::ios::trunc);
    file << "# leading comment\n"
            "server:\n"
            "  address: \"0.0.0.0:12345\"   # inline comment\n"
            "discovery:\n"
            "  ports: [50051, 50052]  # two ports\n"
            "  include_self: true     # keep self\n"
            "  subnets: [\"10.1.0.0/16\"]\n";
  }
  Config commented;
  status = loadConfig(commented, "config_test_comments.yaml");
  check(status.ok(), "commented YAML loads: " + status.toString());
  check(commented.server.address == "0.0.0.0:12345", "inline comment ignored");
  check(commented.discovery.ports.size() == 2 &&
            commented.discovery.ports[1] == 50052,
        "flow sequence with comment parsed");
  check(commented.discovery.include_self, "bool with comment parsed");
  check(commented.discovery.subnets.size() == 1 &&
            commented.discovery.subnets[0] == "10.1.0.0/16",
        "quoted subnet parsed");

  std::cout << "== malformed YAML is rejected ==" << std::endl;
  {
    std::ofstream bad("config_test_bad.yaml",
                      std::ios::binary | std::ios::trunc);
    bad << "server: [unclosed\n";
  }
  Config broken;
  status = loadConfig(broken, "config_test_bad.yaml");
  check(status.code() == StatusCode::InvalidArgument,
        "malformed YAML -> InvalidArgument");

  std::cout << "== local address handling ==" << std::endl;
  check(isLoopbackAddress("127.0.0.1"), "127.0.0.1 is loopback");
  check(isLoopbackAddress("127.8.8.8"), "127.8.8.8 is loopback");
  check(!isLoopbackAddress("192.168.1.1"), "192.168.1.1 is not loopback");
  check(!isLoopbackAddress("10.0.0.1"), "10.0.0.1 is not loopback");

  const auto self_all = localIPv4Addresses(true, true);
  const auto self_up = localIPv4Addresses(false, false);
  check(!self_all.empty(), "self address set is non-empty");
  check(self_all.size() >= self_up.size(),
        "including down interfaces is a superset");
  bool has_loopback = false;
  for (const auto& ip : self_all) {
    if (isLoopbackAddress(ip)) {
      has_loopback = true;
    }
  }
  check(has_loopback, "self set contains a loopback address");

  const auto scanned = listNetworkInterfaces(false, false);
  for (const auto& net : scanned) {
    check(!net.is_loopback, "default interface list excludes loopback");
  }

  std::remove(path.c_str());
  std::remove("config_test_bad.yaml");
  std::remove("config_test_comments.yaml");

  std::cout << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
            << " (" << g_failures << " failures)" << std::endl;
  return g_failures == 0 ? 0 : 1;
}
