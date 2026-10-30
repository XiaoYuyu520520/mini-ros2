// Live diagnostic (not a ctest): what a *different process* sees on
// `/parameter_events`.
//
// The event stream is the one interface to a running node that does not need
// the node's service names to be known in advance, which is what makes it the
// interesting one - and also what makes it easy to get wrong in a way nothing
// notices: a publisher writing into a channel nobody reads looks exactly like a
// node that never changed a parameter. Run this in one window and
// `ros2 param set <node> <name> <value>` in another; the message that arrives,
// or does not, is the whole answer, and it is printed field by field so that a
// wrong list (`changed` where `new` was meant) shows up as itself.
//
//     parameter_events_probe.exe [--count N] [--timeout S]
//
// The node it creates exists to hold the subscription: the ring is opened the
// way a volatile subscriber opens it, from the head, so what is printed is what
// was published after this process started and not what an earlier writer left
// behind.
#include "rcl_interfaces/msg/parameter_event.hpp"
#include "rclcpp/node.hpp"
#include "ros2_shim/parameter_support.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

void printList(const char * kind, const std::vector<rcl_interfaces::msg::Parameter> & parameters)
{
  for (const auto & parameter : parameters) {
    std::printf(
      "  %-7s %s = %s\n", kind, parameter.name.c_str(),
      ros2_shim::format_parameter_value(parameter.value).c_str());
  }
}

}  // namespace

int main(int argc, char ** argv)
{
  int count = 0;
  double timeout_s = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--count" && i + 1 < argc) {
      count = std::atoi(argv[++i]);
    } else if (arg == "--timeout" && i + 1 < argc) {
      timeout_s = std::atof(argv[++i]);
    } else {
      std::fprintf(stderr, "usage: parameter_events_probe [--count N] [--timeout S]\n");
      return 2;
    }
  }

  auto node = std::make_shared<rclcpp::Node>("parameter_events_probe");
  int seen = 0;
  auto subscription = node->create_subscription<rcl_interfaces::msg::ParameterEvent>(
    "/parameter_events", rclcpp::QoS(1000),
    [&seen](const rcl_interfaces::msg::ParameterEvent & event) {
      ++seen;
      std::printf(
        "event %d\n  stamp   %d.%09u\n  node    %s\n", seen,
        static_cast<int>(event.stamp.sec), static_cast<unsigned>(event.stamp.nanosec),
        event.node.c_str());
      printList("new", event.new_parameters);
      printList("changed", event.changed_parameters);
      printList("deleted", event.deleted_parameters);
      std::printf("---\n");
      std::fflush(stdout);
    });

  std::printf("watching /parameter_events (volatile, from the head of the ring)\n");
  std::fflush(stdout);

  const auto started = std::chrono::steady_clock::now();
  for (;;) {
    node->spin_once();
    if (count > 0 && seen >= count) {
      break;
    }
    if (timeout_s > 0 &&
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() > timeout_s)
    {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  std::printf("%d event(s)\n", seen);
  return 0;
}
