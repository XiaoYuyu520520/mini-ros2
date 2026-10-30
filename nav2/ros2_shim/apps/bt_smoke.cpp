// Smoke harness: does a behavior tree load on the shim?
//
// `bt_navigator` reaches ACTIVE and then dies with an access violation inside
// `BtActionServer::on_activate()` -> `loadBehaviorTree()`, with a stack that is
// already gone by the time gdb looks at it. This walks the same three steps with
// a breadcrumb between them, so the crash is pinned to one of them:
//
//   1. a `nav2::LifecycleNode` client node (upstream's BtActionServer creates
//      one, named "_", that every BT node pulls off the blackboard);
//   2. the `BehaviorTreeEngine`, which is what owns the BT.CPP factory;
//   3. `createTreeFromFile()` on the XML given on the command line.
//
// It is a diagnostic, not a test: it is not registered with ctest and it needs
// nothing but the build directory.
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "nav2_behavior_tree/behavior_tree_engine.hpp"
#include "nav2_ros_common/lifecycle_node.hpp"
#include "nav2_ros_common/tf2_factories.hpp"
#include "ros2_shim/params_file.hpp"
#include "behaviortree_cpp/blackboard.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char ** argv)
{
  if (argc < 2) {
    std::fprintf(stderr, "usage: bt_smoke FILE.xml [node-name]\n");
    return 2;
  }
  const std::string xml = argv[1];
  const std::string name = argc > 2 ? argv[2] : "_";
  // Third argument: a behavior-tree search directory. With one, the load walks
  // the same path as BtActionServer::loadBehaviorTree() - register the main
  // file, register every other .xml next to it, then build the tree by ID -
  // instead of the one-shot createTreeFromFile().
  const std::string search_dir = argc > 3 ? argv[3] : "";

  rclcpp::init(argc, argv);
  ros2_shim::installParameterFileLoader();

  auto node = std::make_shared<nav2::LifecycleNode>(name, rclcpp::NodeOptions());
  std::printf("1. client node created: '%s'\n", node->get_name());
  std::fflush(stdout);
  node->configure();
  node->activate();
  std::printf("   client node active\n");
  std::fflush(stdout);

  std::vector<std::string> plugin_libraries;
  nav2_behavior_tree::BehaviorTreeEngine engine(plugin_libraries, node);
  std::printf("2. engine built\n");
  std::fflush(stdout);

  // `BtActionServer::on_configure()` puts these five entries on the blackboard
  // before any tree is built; the BT nodes read them from their constructors, so
  // a tree built without them dies with
  // "Blackboard::get() error. Missing key [bt_loop_duration]".
  auto blackboard = BT::Blackboard::create();
  blackboard->set<nav2::LifecycleNode::SharedPtr>("node", node);
  blackboard->set<std::chrono::milliseconds>("server_timeout", std::chrono::milliseconds(20));
  blackboard->set<std::chrono::milliseconds>("cancel_timeout", std::chrono::milliseconds(500));
  blackboard->set<std::chrono::milliseconds>("bt_loop_duration", std::chrono::milliseconds(10));
  blackboard->set<std::chrono::milliseconds>(
    "wait_for_service_timeout", std::chrono::milliseconds(1000));
  // `BehaviorTreeNavigator::configure()` adds these three; every BT node that
  // looks at a transform reads "tf_buffer" from its constructor.
  blackboard->set<nav2::TransformBuffer::SharedPtr>(
    "tf_buffer", std::make_shared<nav2::TransformBuffer>(node->get_clock()));
  blackboard->set("initial_pose_received", false);
  blackboard->set("number_recoveries", 0);
  std::printf("3. creating tree from %s\n", xml.c_str());
  std::fflush(stdout);

  try {
    if (search_dir.empty()) {
      auto tree = engine.createTreeFromFile(xml, blackboard);
      std::printf("   tree created, root '%s'\n", tree.rootNode()->name().c_str());
    } else {
      auto info = engine.parseTreeInfo(xml);
      engine.registerTreeFromFile(xml);
      std::printf("   main file registered, id '%s'\n", info.main_id.c_str());
      std::fflush(stdout);
      for (const auto & entry : std::filesystem::directory_iterator(search_dir)) {
        if (entry.path().extension() != ".xml" || entry.path().string() == xml) {
          continue;
        }
        std::printf("   registering %s\n", entry.path().filename().string().c_str());
        std::fflush(stdout);
        engine.parseTreeInfo(entry.path().string());
        engine.registerTreeFromFile(entry.path().string());
      }
      std::printf("   all trees registered, building '%s'\n", info.main_id.c_str());
      std::fflush(stdout);
      auto tree = engine.createTree(info.main_id, blackboard);
      std::printf("   tree created by ID, root '%s'\n", tree.rootNode()->name().c_str());
    }
  } catch (const std::exception & ex) {
    std::printf("   tree NOT created: %s\n", ex.what());
  }
  std::fflush(stdout);

  rclcpp::shutdown();
  return 0;
}
