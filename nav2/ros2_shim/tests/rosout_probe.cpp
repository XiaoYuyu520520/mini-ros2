// Live diagnostic (not a ctest): a node that logs, for a *different process* to
// read off `/rosout`.
//
//     rosout_probe.exe [--count N] [--interval S]
//
// Run it in one window and
//
//     ros2_bridge.bat start
//     ros2.bat topic echo /rosout --count 5
//
// in another. What the CLI prints is what the shared-memory ring carries - the
// half of the topic the in-process test (`tests/rosout.cpp`) cannot show, where
// the reader is the writer's own process. It is also how the `file` /
// `function` / `line` fields are best read: a console shows the message, and
// only the message, so the call site is invisible until something else prints
// the message back.
#include "rclcpp/logging.hpp"
#include "rclcpp/node.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

int main(int argc, char ** argv)
{
  int count = 5;
  double interval_s = 0.4;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--count" && i + 1 < argc) {
      count = std::atoi(argv[++i]);
    } else if (arg == "--interval" && i + 1 < argc) {
      interval_s = std::atof(argv[++i]);
    } else {
      std::fprintf(stderr, "usage: rosout_probe [--count N] [--interval S]\n");
      return 2;
    }
  }

  // The node that owns the publisher every line below goes through. Its name is
  // what a reader sees in `Log::name`.
  auto node = std::make_shared<rclcpp::Node>("rosout_probe");

  std::printf("logging %d line(s) on /rosout, %.2f s apart\n", count, interval_s);
  std::fflush(stdout);
  for (int i = 1; i <= count; ++i) {
    // Two severities, so a reader sees the number change and not just the text.
    if (i % 3 == 0) {
      RCLCPP_WARN(node->get_logger(), "probe line %d of %d", i, count);
    } else {
      RCLCPP_INFO(node->get_logger(), "probe line %d of %d", i, count);
    }
    std::this_thread::sleep_for(
      std::chrono::milliseconds(static_cast<int>(interval_s * 1000)));
  }
  std::printf("done\n");
  return 0;
}
