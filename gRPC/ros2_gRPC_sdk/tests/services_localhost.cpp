// End-to-end test for the service and action half of the bridge protocol.
//
// The topic RPCs are covered by `localhost_e2e.cpp`; this one drives
// ListServices / CallService and the action family through the public client
// API against a real gRPC server, so the generated stubs, the servicer and the
// client are all exercised. It uses the mock backend, which needs no ROS2
// installation (see the Python suite for the same checks against the
// simulated robot).
//
// Build with -DROS2_GRPC_SDK_BUILD_TESTS=ON and run the produced
// `ros2_grpc_sdk_services_localhost` executable. Exit code 0 means all checks
// passed.

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"
#include "ros2_grpc_sdk/common/log.hpp"
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

std::string asString(const ros2_grpc_sdk::Bytes& bytes) {
  return std::string(bytes.begin(), bytes.end());
}

ros2_grpc_sdk::Bytes asBytes(const std::string& text) {
  return ros2_grpc_sdk::Bytes(text.begin(), text.end());
}

bool hasService(const std::vector<ros2_grpc_sdk::ServiceInfo>& services,
                const std::string& name) {
  for (const auto& service : services) {
    if (service.name == name) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  using namespace ros2_grpc_sdk;

  setLogLevel(LogLevel::Warn);

  const std::string address = "127.0.0.1:50057";

  std::cout << "== starting mock agent on " << address << " ==" << std::endl;
  auto backend = std::make_shared<MockBackend>();
  Ros2BridgeServer server(backend);
  Status status = server.start(address);
  check(status.ok(), "server.start(): " + status.toString());
  if (!status.ok()) {
    return 1;
  }

  Ros2BridgeClient client;
  status = client.connect(address);
  check(status.ok(), "client.connect(): " + status.toString());
  if (!status.ok()) {
    server.stop();
    return 1;
  }

  std::cout << "== list services ==" << std::endl;
  std::vector<ServiceInfo> services;
  status = client.listServices(services);
  check(status.ok(), "listServices() status: " + status.toString());
  check(services.size() == 2, "two demo services are advertised");
  check(hasService(services, "/demo/echo"), "contains /demo/echo");
  check(hasService(services, "/demo/counter_reset"), "contains /demo/counter_reset");
  for (const auto& service : services) {
    check(!service.type.empty(), service.name + " has a type");
    check(service.source == "local", service.name + " is local");
  }

  std::vector<ServiceInfo> filtered;
  status = client.listServices(filtered, "^/demo/echo$");
  check(status.ok() && filtered.size() == 1, "listServices(filter) selects one");
  filtered.clear();
  status = client.listServices(filtered, "no-such-service");
  check(status.ok() && filtered.empty(), "listServices(filter) that matches nothing");

  std::cout << "== get service info ==" << std::endl;
  ServiceInfo info;
  status = client.getServiceInfo("/demo/echo", info);
  check(status.ok(), "getServiceInfo(/demo/echo) status");
  check(info.type == "std_msgs/msg/String", "getServiceInfo reports the type");
  status = client.getServiceInfo("/nope", info);
  check(status.code() == StatusCode::NotFound, "unknown service -> NotFound");

  std::cout << "== call service ==" << std::endl;
  ServiceCallResult result;
  status = client.callService("/demo/echo", "", asBytes("hello service"), result);
  check(status.ok(), "callService(/demo/echo) status: " + status.toString());
  check(result.success, "callService succeeded: " + result.error);
  check(asString(result.response) == "hello service", "the echo service echoed the request");
  check(result.encoding == "cdr", "the response says how it is encoded");

  status = client.callService("/demo/counter_reset", "std_srvs/srv/Trigger", {},
                              result, "cdr", 2000);
  check(status.ok() && result.success, "callService(/demo/counter_reset) succeeded");
  check(asString(result.response) == "reset ok", "the reset service answered");

  // A service that is not there is a *successful RPC* whose call failed: the
  // transport worked, the service did not answer.
  status = client.callService("/demo/missing", "", {}, result);
  check(status.ok(), "calling a missing service is not a transport error");
  check(!result.success, "the call itself reports failure");
  check(result.error.find("/demo/missing") != std::string::npos,
        "the error names the service: " + result.error);

  std::cout << "== list actions ==" << std::endl;
  std::vector<ActionInfo> actions;
  status = client.listActions(actions);
  check(status.ok(), "listActions() status: " + status.toString());
  check(actions.size() == 1 && actions[0].name == "/demo/count",
        "one demo action is advertised");
  ActionInfo action_info;
  status = client.getActionInfo("/demo/count", action_info);
  check(status.ok() && !action_info.type.empty(), "getActionInfo reports the type");
  status = client.getActionInfo("/nope", action_info);
  check(status.code() == StatusCode::NotFound, "unknown action -> NotFound");

  std::cout << "== send an action goal and follow it ==" << std::endl;
  std::string goal_id;
  status = client.sendActionGoal("/demo/count", "", asBytes("4"), goal_id);
  check(status.ok(), "sendActionGoal() status: " + status.toString());
  check(!goal_id.empty(), "the goal came back with a handle");

  std::vector<ActionFeedback> seen;
  bool saw_terminal = false;
  status = client.followActionGoal("/demo/count", goal_id,
                                   [&](const ActionFeedback& message) {
                                     seen.push_back(message);
                                     if (message.terminal()) {
                                       saw_terminal = true;
                                       return false;  // stop following
                                     }
                                     return true;
                                   },
                                   10000);
  check(status.ok(), "followActionGoal() status: " + status.toString());
  check(saw_terminal, "the stream ended with a terminal message");
  // One "executing, no payload yet" event, `4` feedback messages, one result.
  check(seen.size() == 6, "6 messages for a 4 step goal, got " +
                              std::to_string(seen.size()));
  std::size_t payloads = 0;
  for (const auto& message : seen) {
    if (!message.feedback.empty()) {
      ++payloads;
    }
  }
  check(payloads == 4, "4 feedback payloads, got " + std::to_string(payloads));
  check(!seen.empty() && seen.back().status == GoalStatus::Succeeded,
        "the goal succeeded");
  check(asString(seen.back().result) == "counted to 4",
        "the result says what happened: " + asString(seen.back().result));

  std::cout << "== poll the same goal ==" << std::endl;
  ActionFeedback polled;
  status = client.getActionResult("/demo/count", goal_id, polled, 5000);
  check(status.ok(), "getActionResult() status: " + status.toString());
  check(polled.status == GoalStatus::Succeeded, "a finished goal stays Succeeded");
  check(asString(polled.result) == "counted to 4", "the polled result matches");
  status = client.getActionResult("/demo/count", "no-such-goal", polled, 0);
  check(status.code() == StatusCode::NotFound, "unknown goal -> NotFound");

  std::cout << "== cancel a running goal ==" << std::endl;
  std::string long_goal;
  status = client.sendActionGoal("/demo/count", "", asBytes("1000"), long_goal);
  check(status.ok(), "sendActionGoal(1000) status");
  // Let a couple of feedback messages through, then stop it.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ActionFeedback running;
  client.getActionResult("/demo/count", long_goal, running, 0);
  check(running.status == GoalStatus::Executing ||
            running.status == GoalStatus::Accepted,
        "a running goal reports Executing");

  status = client.cancelActionGoal("/demo/count", long_goal);
  check(status.ok(), "cancelActionGoal() status: " + status.toString());

  ActionFeedback cancelled;
  status = client.getActionResult("/demo/count", long_goal, cancelled, 5000);
  check(status.ok(), "getActionResult() after cancel");
  check(cancelled.status == GoalStatus::Canceled,
        "the goal ends up Canceled, not Succeeded");
  status = client.cancelActionGoal("/demo/count", long_goal);
  check(status.code() == StatusCode::InvalidArgument,
        "cancelling a finished goal is refused");

  std::cout << "== unknown goal and action ==" << std::endl;
  std::string ignored;
  status = client.sendActionGoal("/no-such-action", "", {}, ignored);
  check(status.code() == StatusCode::NotFound, "unknown action -> NotFound");

  std::cout << "== stop server ==" << std::endl;
  client.disconnect();
  server.stop();
  check(!server.running(), "server stopped");

  std::cout << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
            << " (" << g_failures << " failures)" << std::endl;
  return g_failures == 0 ? 0 : 1;
}
