#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_srvs/srv/set_bool.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
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

void test_pubsub()
{
  std::printf("[test] publisher / subscription / wall timer\n");
  auto node_a = std::make_shared<rclcpp::Node>("node_a");
  auto node_b = std::make_shared<rclcpp::Node>("node_b");

  auto publisher = node_a->create_publisher<std_msgs::msg::String>("shim_topic", 10);
  std::atomic<int> received{0};
  std::string last;
  auto subscription = node_b->create_subscription<std_msgs::msg::String>(
    "shim_topic", 10,
    [&](const std_msgs::msg::String::ConstSharedPtr msg) {
      last = msg->data;
      received.fetch_add(1);
    });

  auto timer = node_a->create_wall_timer(
    std::chrono::milliseconds(5), [&]() {
      std_msgs::msg::String message;
      message.data = "hello_shm";
      publisher->publish(message);
    });

  for (int i = 0; i < 2000 && received.load() < 5; ++i) {
    node_a->spin_once();
    node_b->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  timer->cancel();

  CHECK(received.load() >= 5);
  CHECK(last == "hello_shm");
  std::printf("  received=%d last='%s'\n", received.load(), last.c_str());
}

void test_parameters()
{
  std::printf("[test] parameters\n");
  auto node = std::make_shared<rclcpp::Node>("param_node");

  CHECK(node->declare_parameter("int_param", 42) == 42);
  int int_value = 0;
  CHECK(node->get_parameter("int_param", int_value));
  CHECK(int_value == 42);

  CHECK(node->declare_parameter("string_param", std::string("abc")) == "abc");
  std::string string_value;
  CHECK(node->get_parameter("string_param", string_value));
  CHECK(string_value == "abc");

  double fallback = 0.0;
  node->get_parameter_or("missing_param", fallback, 3.5);
  CHECK(fallback == 3.5);

  bool callback_called = false;
  node->add_on_set_parameters_callback(
    [&](const std::vector<rclcpp::Parameter> & params) {
      callback_called = true;
      rcl_interfaces::msg::SetParametersResult result;
      result.successful = true;
      (void)params;
      return result;
    });

  auto result = node->set_parameter(rclcpp::Parameter("int_param", 7));
  CHECK(result.successful);
  CHECK(callback_called);
  CHECK(node->get_parameter("int_param", int_value));
  CHECK(int_value == 7);
  CHECK(node->has_parameter("int_param"));
  std::printf("  int_param=%d\n", int_value);
}

void test_service()
{
  std::printf("[test] service / client\n");
  auto server_node = std::make_shared<rclcpp::Node>("service_node");
  auto client_node = std::make_shared<rclcpp::Node>("client_node");

  using SetBool = std_srvs::srv::SetBool;
  bool server_called = false;
  auto service = server_node->create_service<SetBool>(
    "shim_service",
    [&](std::shared_ptr<SetBool::Request> request, std::shared_ptr<SetBool::Response> response) {
      server_called = true;
      response->success = request->data;
      response->message = "ok";
    });
  CHECK(service != nullptr);

  auto client = client_node->create_client<SetBool>("shim_service");
  auto request = std::make_shared<SetBool::Request>();
  request->data = true;
  auto future = client->async_send_request(request);

  for (int i = 0; i < 2000 && future.wait_for(std::chrono::milliseconds(0)) !=
    std::future_status::ready; ++i)
  {
    server_node->spin_once();
    client_node->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  CHECK(future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
  CHECK(server_called);
  if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
    auto response = future.get();
    CHECK(response->success);
    CHECK(response->message == "ok");
    std::printf("  response.success=%d message='%s'\n", response->success,
      response->message.c_str());
  }
}

}  // namespace

int main()
{
  std::printf("=== rclcpp shim tests ===\n");
  rclcpp::init(0, nullptr);
  test_pubsub();
  test_parameters();
  test_service();
  rclcpp::shutdown();
  if (g_failures == 0) {
    std::printf("=== ALL TESTS PASSED ===\n");
    return 0;
  }
  std::printf("=== %d CHECK(S) FAILED ===\n", g_failures);
  return 1;
}
