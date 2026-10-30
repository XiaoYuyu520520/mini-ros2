// The lifecycle services the nav2 servers expose, and the state machine behind
// them.
//
// `planner_server`, `controller_server` and `bt_navigator` build their
// costmaps, plugins and action servers in on_configure(), so until something
// asks them to configure they publish nothing at all. Upstream that something
// is nav2_lifecycle_manager, calling `/<node>/change_state` -
// nav2_util::LifecycleServiceClient's change_state() and get_state() are the
// only two service calls it makes. This test drives a lifecycle node through
// exactly those calls and checks the state machine, the states/transitions
// queries, and that a callback which fails or throws is reported as a failed
// transition instead of taking the process down.
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"

#include "lifecycle_msgs/msg/state.hpp"
#include "lifecycle_msgs/msg/transition.hpp"
#include "lifecycle_msgs/srv/change_state.hpp"
#include "lifecycle_msgs/srv/get_available_states.hpp"
#include "lifecycle_msgs/srv/get_available_transitions.hpp"
#include "lifecycle_msgs/srv/get_state.hpp"

#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{

int g_failures = 0;

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::printf("  FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
      ++g_failures; \
    } \
  } while (0)

using Transition = lifecycle_msgs::msg::Transition;
using StateMessage = lifecycle_msgs::msg::State;

// Stands in for one of the ported servers. `mode` decides what the callbacks
// do, so the failure paths can be exercised with the same plumbing.
class ProbeNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  enum class Mode
  {
    HAPPY,
    CONFIGURE_FAILS,
    CONFIGURE_THROWS,
  };

  explicit ProbeNode(const std::string & name, Mode mode)
  : rclcpp_lifecycle::LifecycleNode(name, "", rclcpp::NodeOptions(), true /*services*/),
    mode_(mode)
  {
  }

  std::vector<std::string> calls;
  // What `get_current_state()` returned while the last callback ran, and the
  // state the callback itself was handed.
  uint8_t state_seen_by_callback = StateMessage::PRIMARY_STATE_UNKNOWN;
  uint8_t argument_seen_by_callback = StateMessage::PRIMARY_STATE_UNKNOWN;

  CallbackReturn on_configure(const rclcpp_lifecycle::State & previous) override
  {
    calls.push_back("configure");
    note(previous);
    if (mode_ == Mode::CONFIGURE_FAILS) {
      return CallbackReturn::FAILURE;
    }
    if (mode_ == Mode::CONFIGURE_THROWS) {
      throw std::runtime_error("configure callback threw on purpose");
    }
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_activate(const rclcpp_lifecycle::State & previous) override
  {
    calls.push_back("activate");
    note(previous);
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State & previous) override
  {
    calls.push_back("deactivate");
    note(previous);
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State & previous) override
  {
    calls.push_back("cleanup");
    note(previous);
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_shutdown(const rclcpp_lifecycle::State & previous) override
  {
    calls.push_back("shutdown");
    note(previous);
    return CallbackReturn::SUCCESS;
  }

private:
  // nav2::LifecycleNode::create_publisher reads the state back from inside
  // on_configure() to decide whether the publisher it just made should be
  // active, so reading it here has to work rather than deadlock.
  void note(const rclcpp_lifecycle::State & previous)
  {
    state_seen_by_callback = get_current_state().id();
    argument_seen_by_callback = previous.id();
  }

  Mode mode_;
};

// Sends one request and spins both nodes until the response comes back, which
// is what a lifecycle manager does while it waits on a transition.
template<class ClientT, class RequestT>
std::shared_ptr<typename ClientT::Response> call_service(
  const std::shared_ptr<ClientT> & client, const std::shared_ptr<RequestT> & request,
  rclcpp_lifecycle::LifecycleNode & server, rclcpp::Node & client_node)
{
  auto future = client->async_send_request(request);
  for (int i = 0;
    i < 2000 && future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready; ++i)
  {
    server.spin_once();
    client_node.spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    return nullptr;
  }
  return future.get();
}

// One changed state, as the manager asks for it: `/<node>/change_state`.
bool change_state(
  const std::shared_ptr<rclcpp::Client<lifecycle_msgs::srv::ChangeState>> & client,
  uint8_t transition, rclcpp_lifecycle::LifecycleNode & server, rclcpp::Node & client_node)
{
  auto request = std::make_shared<lifecycle_msgs::srv::ChangeState::Request>();
  request->transition.id = transition;
  const auto response = call_service(client, request, server, client_node);
  if (!response) {
    std::printf("  FAIL: change_state(%u) never answered\n", transition);
    ++g_failures;
    return false;
  }
  return response->success;
}

uint8_t get_state(
  const std::shared_ptr<rclcpp::Client<lifecycle_msgs::srv::GetState>> & client,
  rclcpp_lifecycle::LifecycleNode & server, rclcpp::Node & client_node)
{
  auto request = std::make_shared<lifecycle_msgs::srv::GetState::Request>();
  const auto response = call_service(client, request, server, client_node);
  if (!response) {
    std::printf("  FAIL: get_state never answered\n");
    ++g_failures;
    return StateMessage::PRIMARY_STATE_UNKNOWN;
  }
  return response->current_state.id;
}

void test_state_machine()
{
  std::printf("[test] /<node>/change_state drives the state machine\n");
  auto server = std::make_shared<ProbeNode>("lifecycle_probe_happy", ProbeNode::Mode::HAPPY);
  auto client_node = std::make_shared<rclcpp::Node>("lifecycle_probe_client");

  auto change = client_node->create_client<lifecycle_msgs::srv::ChangeState>(
    "lifecycle_probe_happy/change_state");
  auto state = client_node->create_client<lifecycle_msgs::srv::GetState>(
    "lifecycle_probe_happy/get_state");

  CHECK(get_state(state, *server, *client_node) == StateMessage::PRIMARY_STATE_UNCONFIGURED);

  CHECK(change_state(change, Transition::TRANSITION_CONFIGURE, *server, *client_node));
  CHECK(server->get_current_state().id() == StateMessage::PRIMARY_STATE_INACTIVE);
  CHECK(get_state(state, *server, *client_node) == StateMessage::PRIMARY_STATE_INACTIVE);
  // While the callback ran the node reported the transition state, and the
  // callback was handed the state the node was leaving.
  CHECK(server->state_seen_by_callback == StateMessage::TRANSITION_STATE_CONFIGURING);
  CHECK(server->argument_seen_by_callback == StateMessage::PRIMARY_STATE_UNCONFIGURED);

  CHECK(change_state(change, Transition::TRANSITION_ACTIVATE, *server, *client_node));
  CHECK(server->get_current_state().id() == StateMessage::PRIMARY_STATE_ACTIVE);
  CHECK(get_state(state, *server, *client_node) == StateMessage::PRIMARY_STATE_ACTIVE);
  CHECK(server->state_seen_by_callback == StateMessage::TRANSITION_STATE_ACTIVATING);
  CHECK(server->argument_seen_by_callback == StateMessage::PRIMARY_STATE_INACTIVE);

  CHECK(change_state(change, Transition::TRANSITION_DEACTIVATE, *server, *client_node));
  CHECK(get_state(state, *server, *client_node) == StateMessage::PRIMARY_STATE_INACTIVE);
  CHECK(server->state_seen_by_callback == StateMessage::TRANSITION_STATE_DEACTIVATING);
  CHECK(server->argument_seen_by_callback == StateMessage::PRIMARY_STATE_ACTIVE);

  CHECK(change_state(change, Transition::TRANSITION_CLEANUP, *server, *client_node));
  CHECK(get_state(state, *server, *client_node) == StateMessage::PRIMARY_STATE_UNCONFIGURED);
  CHECK(server->state_seen_by_callback == StateMessage::TRANSITION_STATE_CLEANINGUP);
  CHECK(server->argument_seen_by_callback == StateMessage::PRIMARY_STATE_INACTIVE);

  CHECK(change_state(change, Transition::TRANSITION_UNCONFIGURED_SHUTDOWN, *server, *client_node));
  CHECK(get_state(state, *server, *client_node) == StateMessage::PRIMARY_STATE_FINALIZED);
  CHECK(server->state_seen_by_callback == StateMessage::TRANSITION_STATE_SHUTTINGDOWN);
  CHECK(server->argument_seen_by_callback == StateMessage::PRIMARY_STATE_UNCONFIGURED);

  const std::string expected = "configure,activate,deactivate,cleanup,shutdown";
  std::string actual;
  for (const auto & call : server->calls) {
    if (!actual.empty()) {
      actual += ",";
    }
    actual += call;
  }
  CHECK(actual == expected);
  std::printf("  callbacks: %s\n", actual.c_str());
}

void test_queries()
{
  std::printf("[test] /<node>/get_available_states and .../get_available_transitions\n");
  auto server = std::make_shared<ProbeNode>("lifecycle_probe_query", ProbeNode::Mode::HAPPY);
  auto client_node = std::make_shared<rclcpp::Node>("lifecycle_probe_query_client");

  auto change = client_node->create_client<lifecycle_msgs::srv::ChangeState>(
    "lifecycle_probe_query/change_state");
  auto states = client_node->create_client<lifecycle_msgs::srv::GetAvailableStates>(
    "lifecycle_probe_query/get_available_states");
  auto transitions = client_node->create_client<lifecycle_msgs::srv::GetAvailableTransitions>(
    "lifecycle_probe_query/get_available_transitions");

  auto states_response = call_service(
    states, std::make_shared<lifecycle_msgs::srv::GetAvailableStates::Request>(),
    *server, *client_node);
  CHECK(states_response != nullptr);
  if (states_response) {
    CHECK(states_response->available_states.size() == 10);
    std::printf("  %zu available states\n", states_response->available_states.size());
  }

  auto unconfigured = call_service(
    transitions, std::make_shared<lifecycle_msgs::srv::GetAvailableTransitions::Request>(),
    *server, *client_node);
  CHECK(unconfigured != nullptr);
  if (unconfigured) {
    // unconfigured offers exactly `configure` and `shutdown`
    CHECK(unconfigured->available_transitions.size() == 2);
    CHECK(unconfigured->available_transitions[0].transition.id == Transition::TRANSITION_CONFIGURE);
    CHECK(unconfigured->available_transitions[0].start_state.id ==
      StateMessage::PRIMARY_STATE_UNCONFIGURED);
    CHECK(unconfigured->available_transitions[0].goal_state.id ==
      StateMessage::PRIMARY_STATE_INACTIVE);
  }

  CHECK(change_state(change, Transition::TRANSITION_CONFIGURE, *server, *client_node));
  auto inactive = call_service(
    transitions, std::make_shared<lifecycle_msgs::srv::GetAvailableTransitions::Request>(),
    *server, *client_node);
  CHECK(inactive != nullptr);
  if (inactive) {
    // inactive offers `cleanup`, `activate` and `shutdown`
    CHECK(inactive->available_transitions.size() == 3);
    CHECK(inactive->available_transitions[1].transition.id == Transition::TRANSITION_ACTIVATE);
    CHECK(inactive->available_transitions[1].goal_state.id == StateMessage::PRIMARY_STATE_ACTIVE);
    std::printf(
      "  %zu available transitions from inactive\n", inactive->available_transitions.size());
  }
}

void test_failed_callbacks()
{
  std::printf("[test] a failing or throwing callback is a failed transition, not a crash\n");
  auto client_node = std::make_shared<rclcpp::Node>("lifecycle_probe_failure_client");

  const std::pair<std::string, ProbeNode::Mode> probes[] = {
    {"lifecycle_probe_fails", ProbeNode::Mode::CONFIGURE_FAILS},
    {"lifecycle_probe_throws", ProbeNode::Mode::CONFIGURE_THROWS}};

  for (const auto & probe : probes) {
    auto server = std::make_shared<ProbeNode>(probe.first, probe.second);
    auto change = client_node->create_client<lifecycle_msgs::srv::ChangeState>(
      probe.first + "/change_state");
    auto state = client_node->create_client<lifecycle_msgs::srv::GetState>(
      probe.first + "/get_state");

    CHECK(!change_state(change, Transition::TRANSITION_CONFIGURE, *server, *client_node));
    CHECK(get_state(state, *server, *client_node) == StateMessage::PRIMARY_STATE_UNCONFIGURED);
    CHECK(server->calls.size() == 1);
    std::printf("  %s: transition refused, still answering services\n", probe.first.c_str());
  }
}

}  // namespace

int main()
{
  std::printf("=== rclcpp_lifecycle services ===\n");
  rclcpp::init(0, nullptr);
  test_state_machine();
  test_queries();
  test_failed_callbacks();
  rclcpp::shutdown();

  if (g_failures == 0) {
    std::printf("=== ALL TESTS PASSED ===\n");
    return 0;
  }
  std::printf("=== %d CHECK(S) FAILED ===\n", g_failures);
  return 1;
}
