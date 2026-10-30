// LAN discovery end-to-end test. A mock agent is bound to loopback and the
// discovery module is pointed at 127.0.0.1/32 (with self-inclusion enabled,
// since loopback is normally excluded) to verify the full probe -> verify ->
// sort -> connect path.

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"
#include "ros2_grpc_sdk/common/log.hpp"
#include "ros2_grpc_sdk/common/types.hpp"
#include "ros2_grpc_sdk/discovery/discovery.hpp"
#include "ros2_grpc_sdk/server/mock_backend.hpp"
#include "ros2_grpc_sdk/server/ros2_bridge_server.hpp"

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

}  // namespace

int main() {
  using namespace ros2_grpc_sdk;
  setLogLevel(LogLevel::Warn);

  const std::uint16_t port = 50057;

  std::cout << "== cidr expansion ==" << std::endl;
  const auto hosts = expandCidr("192.168.1.0/24", 4096);
  check(hosts.size() == 254, "192.168.1.0/24 -> 254 usable hosts");
  check(hosts.front() == "192.168.1.1", "first host is .1");
  check(hosts.back() == "192.168.1.254", "last host is .254");
  const auto excluded = expandCidr("192.168.1.0/24", 4096, {"192.168.1.10"});
  check(excluded.size() == 253, "excluded host removed");

  std::cout << "== start agent on 127.0.0.1:" << port << " ==" << std::endl;
  auto backend = std::make_shared<MockBackend>();
  Ros2BridgeServer server(backend);
  Status status = server.start("127.0.0.1:" + std::to_string(port));
  check(status.ok(), "server.start(): " + status.toString());
  if (!status.ok()) {
    return 1;
  }

  DiscoveryOptions options;
  options.ports = {port};
  options.subnets = {"127.0.0.1/32"};
  options.include_self = true;      // loopback/self is excluded by default
  options.connect_timeout_ms = 300;
  options.ping_timeout_ms = 1000;
  options.overall_timeout_ms = 4000;

  std::cout << "== discovery finds the agent ==" << std::endl;
  Ros2BridgeDiscovery discovery(options);
  std::vector<DiscoveredAgent> agents;
  status = discovery.discover(agents);
  check(status.ok(), "discover() status: " + status.toString());
  check(agents.size() == 1, "exactly one agent found");
  if (!agents.empty()) {
    check(agents[0].host == "127.0.0.1", "agent host is 127.0.0.1");
    check(agents[0].port == port, "agent port matches");
    check(agents[0].ros_distro == "mock", "agent ros_distro is 'mock'");
    check(agents[0].latency_ms >= 0.0, "latency measured");
  }

  std::cout << "== discoverBest returns the lowest latency endpoint ==" << std::endl;
  DiscoveredAgent best;
  status = discovery.discoverBest(best);
  check(status.ok() && best.port == port, "discoverBest() picked the agent");

  std::cout << "== connect to nearest agent ==" << std::endl;
  Ros2BridgeClient client;
  DiscoveredAgent chosen;
  status = client.connectToNearestAgent(options, &chosen);
  check(status.ok(), "connectToNearestAgent(): " + status.toString());
  check(client.connected(), "client connected");
  check(chosen.port == port, "chosen port matches");
  std::vector<TopicInfo> topics;
  status = client.listTopics(topics);
  check(status.ok() && !topics.empty(), "listTopics() on the chosen agent");
  client.disconnect();

  std::cout << "== self/loopback excluded by default ==" << std::endl;
  DiscoveryOptions strict = options;
  strict.include_self = false;
  // A whole loopback range: without exclusion this would find the agent on
  // 127.0.0.1, but self/loopback must be dropped.
  strict.subnets = {"127.0.0.0/29"};
  Ros2BridgeDiscovery strict_discovery(strict);
  std::vector<DiscoveredAgent> none;
  status = strict_discovery.discover(none);
  check(!status.ok() || none.empty(), "loopback/self not reported by default");

  std::cout << "== stop server ==" << std::endl;
  server.stop();
  check(!server.running(), "server stopped");

  std::cout << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
            << " (" << g_failures << " failures)" << std::endl;
  return g_failures == 0 ? 0 : 1;
}
