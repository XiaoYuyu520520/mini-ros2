// Localhost end-to-end test for the ros2_gRPC_sdk. It spins up the mock agent
// in-process and drives it through the public client API, mirroring exactly
// what a Windows machine would do against a Linux ROS2 agent.
//
// Build with -DROS2_GRPC_SDK_BUILD_TESTS=ON and run the produced
// `ros2_grpc_sdk_localhost_e2e` executable. Exit code 0 means all checks
// passed.

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"
#include "ros2_grpc_sdk/common/log.hpp"
#include "ros2_grpc_sdk/common/ros2_types.hpp"
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

bool waitFor(const std::function<bool()>& predicate, int timeout_ms) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return predicate();
}

bool containsTopic(const std::vector<ros2_grpc_sdk::TopicInfo>& topics,
                   const std::string& name) {
  for (const auto& topic : topics) {
    if (topic.name == name) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  using namespace ros2_grpc_sdk;

  setLogLevel(LogLevel::Warn);

  const std::string address = "127.0.0.1:50056";

  std::cout << "== starting mock agent on " << address << " ==" << std::endl;
  auto backend = std::make_shared<MockBackend>();
  Ros2BridgeServer server(backend);
  Status status = server.start(address);
  check(status.ok(), "server.start(): " + status.toString());
  if (!status.ok()) {
    return 1;
  }

  std::cout << "== connecting client ==" << std::endl;
  Ros2BridgeClient client;
  status = client.connect(address);
  check(status.ok(), "client.connect(): " + status.toString());
  check(client.connected(), "client.connected()");
  if (!status.ok()) {
    server.stop();
    return 1;
  }

  std::cout << "== ping ==" << std::endl;
  std::string echo;
  status = client.ping("ping-payload", echo);
  check(status.ok(), "ping() status");
  check(echo == "ping-payload", "ping() echoed payload");

  std::cout << "== list topics ==" << std::endl;
  std::vector<TopicInfo> topics;
  status = client.listTopics(topics);
  check(status.ok(), "listTopics() status");
  check(containsTopic(topics, "/demo/chatter"), "contains /demo/chatter");
  check(containsTopic(topics, "/demo/counter"), "contains /demo/counter");

  std::vector<TopicInfo> filtered;
  status = client.listTopics(filtered, "^/demo/");
  check(status.ok(), "listTopics(filter) status");
  check(filtered.size() == 3, "filter '^/demo/' returns 3 topics");

  std::cout << "== get topic info ==" << std::endl;
  TopicInfo info;
  status = client.getTopicInfo("/demo/counter", info);
  check(status.ok(), "getTopicInfo() status");
  check(info.type == "example_interfaces/msg/Int64",
        "getTopicInfo() resolves type");
  status = client.getTopicInfo("/does/not/exist", info);
  check(status.code() == StatusCode::NotFound,
        "unknown topic -> NotFound");

  std::cout << "== live subscription ==" << std::endl;
  std::atomic<int> live_count{0};
  std::string last_live;
  status = client.subscribe(
      "/demo/chatter", "std_msgs/msg/String",
      [&live_count, &last_live](const Message& message) {
        last_live = ros2::decodeString(message.data);
        live_count.fetch_add(1);
      });
  check(status.ok(), "subscribe(/demo/chatter) status: " + status.toString());
  check(client.isSubscribed("/demo/chatter"), "isSubscribed(/demo/chatter)");
  check(waitFor([&live_count]() { return live_count.load() > 0; }, 3000),
        "received a live synthetic message");
  check(last_live.rfind("mock /demo/chatter #", 0) == 0,
        "live payload decodes as CDR string: '" + last_live + "'");

  status = client.subscribe("/demo/chatter", "std_msgs/msg/String",
                            [](const Message&) {});
  check(status.code() == StatusCode::AlreadyExists,
        "duplicate subscribe -> AlreadyExists");

  std::cout << "== publish / loopback ==" << std::endl;
  std::atomic<int> echoed{0};
  std::string echoed_payload;
  status = client.subscribe(
      "/demo/echo", "std_msgs/msg/String",
      [&echoed, &echoed_payload](const Message& message) {
        echoed_payload = ros2::decodeString(message.data);
        echoed.fetch_add(1);
      });
  check(status.ok(), "subscribe(/demo/echo) status");

  const std::string outbound = "hello-ros2-over-grpc";
  status = client.publish("/demo/echo", "std_msgs/msg/String",
                          ros2::encodeString(outbound));
  check(status.ok(), "publish(/demo/echo) status: " + status.toString());
  // The server-side subscription is registered asynchronously by the gRPC
  // streaming handler, so retry until the loopback path is live (this mirrors
  // a real publisher that keeps publishing).
  for (int attempt = 0; attempt < 60 && echoed.load() == 0; ++attempt) {
    client.publish("/demo/echo", "std_msgs/msg/String",
                   ros2::encodeString(outbound));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  check(waitFor([&echoed]() { return echoed.load() > 0; }, 2000),
        "observed loopback message");
  check(echoed_payload == outbound,
        "loopback payload preserved ('" + echoed_payload + "')");

  std::cout << "== int64 round trip ==" << std::endl;
  std::atomic<std::int64_t> received_value{0};
  status = client.subscribe(
      "/demo/int", "example_interfaces/msg/Int64",
      [&received_value](const Message& message) {
        received_value.store(ros2::decodeInt64(message.data));
      });
  check(status.ok(), "subscribe(/demo/int) status");
  for (int attempt = 0; attempt < 60 && received_value.load() != 424242;
       ++attempt) {
    client.publish("/demo/int", "example_interfaces/msg/Int64",
                   ros2::encodeInt64(424242));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  check(waitFor([&received_value]() { return received_value.load() == 424242; },
                2000),
        "int64 loopback value 424242");

  std::cout << "== unsubscribe ==" << std::endl;
  status = client.unsubscribe("/demo/echo");
  check(status.ok(), "unsubscribe(/demo/echo) status");
  check(!client.isSubscribed("/demo/echo"), "isSubscribed(/demo/echo) == false");
  status = client.unsubscribe("/demo/echo");
  check(status.code() == StatusCode::NotFound,
        "second unsubscribe -> NotFound");

  std::cout << "== server side count reflects subscriber ==" << std::endl;
  status = client.getTopicInfo("/demo/chatter", info);
  check(status.ok() && info.subscriber_count == 1,
        "getTopicInfo(/demo/chatter) sub count == 1");

  std::cout << "== disconnect ==" << std::endl;
  client.disconnect();
  check(!client.connected(), "client disconnected");
  status = client.listTopics(topics);
  check(status.code() == StatusCode::Unavailable,
        "listTopics after disconnect -> Unavailable");

  std::cout << "== stop server ==" << std::endl;
  server.stop();
  check(!server.running(), "server stopped");

  std::cout << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
            << " (" << g_failures << " failures)" << std::endl;
  return g_failures == 0 ? 0 : 1;
}
