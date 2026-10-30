// Copyright (c) 2018 Intel Corporation
// Copyright (c) 2020 Florian Gramss
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "nav2_behavior_tree/behavior_tree_engine.hpp"

// PORT (ros2_shim): upstream loads each nav2 BT node from its own plugin library
// (see `registerFromPlugin` below). The Windows port links them all into this
// binary instead, so they register themselves through this call - the same one
// `plugins_list.hpp` would have produced for a plugin library, and it keeps the
// generated node list in one place.
#include "ros2_shim/bt_node_registry.hpp"

#include <memory>
#include <string>
#include <vector>
#include "tinyxml2.h" //NOLINT

#include "rclcpp/rclcpp.hpp"
#include "behaviortree_cpp/json_export.h"
#include "behaviortree_cpp/utils/shared_library.h"
#include "nav2_behavior_tree/json_utils.hpp"
#include "nav2_behavior_tree/utils/loop_rate.hpp"
#include <cstdlib>
#include <iostream>
// PORT (ros2_shim): the status-change logger behind NAV2_SHIM_BT_TRACE
// (see `run` below). Upstream never attaches it; it is the only way to see
// which node of a tree is holding a goal when the tick loop is alive but the
// tree is not progressing.
#include "behaviortree_cpp/loggers/bt_cout_logger.h"
#include "nav2_ros_common/rate.hpp"

namespace nav2_behavior_tree
{

BehaviorTreeEngine::BehaviorTreeEngine(
  const std::vector<std::string> & plugin_libraries,
  const nav2::LifecycleNode::SharedPtr node)
{
  BT::SharedLibrary loader;
  for (const auto & p : plugin_libraries) {
    // PORT (ros2_shim): an empty entry is skipped. Upstream never sees one -
    // `plugin_lib_names` comes from the generated `plugins_list.hpp`, and
    // upstream's `@plugin_libs@` is a list of real shared libraries. The
    // Windows port links every BT node into the binary instead, so its
    // `plugins_list.hpp` is an empty string, and `nav2_util::split("", ';')`
    // returns one empty token rather than none. Without this guard that token
    // reaches BehaviorTree.CPP as a library name, which appends ".dll" and
    // fails with `Could not load library: .dll` - surfacing as
    // "Failed to create navigator id navigate_to_pose".
    if (p.empty()) {
      continue;
    }
    factory_.registerFromPlugin(loader.getOSName(p));
  }

  // PORT (ros2_shim): the counterpart of the include above.
  nav2_shim::bt_registry::registerNodes(factory_);

  node_ = node;
}

BtStatus
BehaviorTreeEngine::run(
  BT::Tree * tree,
  std::function<void()> onLoop,
  std::function<bool()> cancelRequested,
  std::chrono::milliseconds loopTimeout)
{
  auto node = node_.lock();
  if (!node) {
    RCLCPP_ERROR(
      rclcpp::get_logger("BehaviorTreeEngine"),
      "BehaviorTreeEngine node expired. Exiting with failure.");
    return BtStatus::FAILED;
  }

  auto rate_clock = nav2::selectSteadyOrSimClock(node);
  nav2_behavior_tree::LoopRate loopRate(loopTimeout, tree, rate_clock);
  // PORT (ros2_shim): NAV2_SHIM_BT_TRACE=1 prints one line per node status
  // change of this tree, on stdout (which the stack launcher redirects into
  // each node's log). BehaviorTree.CPP's own `StdCoutLogger` is what does the
  // printing; it is attached here rather than in the navigator so that the
  // trace covers exactly one goal's worth of execution.
  const bool bt_trace = std::getenv("NAV2_SHIM_BT_TRACE") != nullptr;
  std::unique_ptr<BT::StdCoutLogger> status_logger;
  if (bt_trace) {
    status_logger = std::make_unique<BT::StdCoutLogger>(*tree);
  }
  // PORT (ros2_shim): the heartbeat the status logger cannot give. A tree that
  // has stopped progressing looks the same in the log whether its tick loop is
  // spinning in a node or has stopped being called at all, and the two have
  // different causes. One line a second names the difference: it is printed
  // between `tickOnce()` calls, so it keeps appearing while the loop runs and
  // stops the moment a node blocks inside its own tick - and it carries the
  // status of every node, so a tree that is alive and stuck names the node
  // holding it.
  uint64_t tick_count = 0;
  auto trace_heartbeat = [&]() {
    if (!bt_trace || (++tick_count % 30) != 0) {
      return;
    }
    std::string dump;
    BT::applyRecursiveVisitor(
      tree->rootNode(), [&dump](const BT::TreeNode * n) {
        dump += n->name();
        dump += "=";
        dump += BT::toStr(n->status(), false);
        dump += " ";
      });
    std::cout << "[bt-trace] ticks=" << tick_count << " " << dump << std::endl;
  };
  BT::NodeStatus result = BT::NodeStatus::RUNNING;

  // Loop until something happens with ROS or the node completes
  try {
    while (rclcpp::ok() && result == BT::NodeStatus::RUNNING) {
      if (cancelRequested()) {
        tree->haltTree();
        return BtStatus::CANCELED;
      }

      result = tree->tickOnce();

      trace_heartbeat();

      if (result == BT::NodeStatus::RUNNING || result == BT::NodeStatus::IDLE) {
        onLoop();
      }

      if (!loopRate.sleep()) {
        RCLCPP_DEBUG_THROTTLE(
          rclcpp::get_logger("BehaviorTreeEngine"),
          *rate_clock, 1000,
          "Behavior Tree tick rate %0.2f was exceeded!",
          1.0 / (loopRate.period().count() * 1.0e-9));
      }
    }
  } catch (const BT::NodeExecutionError & ex) {
    RCLCPP_ERROR(
      rclcpp::get_logger("BehaviorTreeEngine"),
      "BT Exception at Node: [%s] (Path: %s). Original error: %s. Exiting with failure.",
      ex.failedNode().registration_name.c_str(),
      ex.failedNode().node_path.c_str(),
      ex.originalMessage().c_str());
    return BtStatus::FAILED;
  } catch (const std::exception & ex) {
    RCLCPP_ERROR(
      rclcpp::get_logger("BehaviorTreeEngine"),
      "Behavior tree threw exception: %s. Exiting with failure.", ex.what());
    return BtStatus::FAILED;
  }

  return (result == BT::NodeStatus::SUCCESS) ? BtStatus::SUCCEEDED : BtStatus::FAILED;
}

BT::Tree
BehaviorTreeEngine::createTreeFromText(
  const std::string & xml_string,
  BT::Blackboard::Ptr blackboard)
{
  return factory_.createTreeFromText(xml_string, blackboard);
}

BT::Tree
BehaviorTreeEngine::createTreeFromFile(
  const std::string & file_path,
  BT::Blackboard::Ptr blackboard)
{
  return factory_.createTreeFromFile(file_path, blackboard);
}

BTInfo BehaviorTreeEngine::parseTreeInfo(const std::string & filename)
{
  BTInfo info;
  if (filename.empty()) {
    RCLCPP_ERROR(rclcpp::get_logger("BehaviorTreeEngine"), "Empty BT file path.");
    return info;
  }

  tinyxml2::XMLDocument doc;
  if (doc.LoadFile(filename.c_str()) != tinyxml2::XML_SUCCESS) {
    RCLCPP_ERROR(rclcpp::get_logger("BehaviorTreeEngine"), "Could not parse: %s", filename.c_str());
    return info;
  }

  tinyxml2::XMLElement * root = doc.RootElement();
  if (!root) {
    RCLCPP_ERROR(rclcpp::get_logger("BehaviorTreeEngine"), "No root element in: %s",
      filename.c_str());
    return info;
  }

  // Loop through all BehaviorTree elements to get all IDs
  for (auto * bt = root->FirstChildElement("BehaviorTree"); bt;
    bt = bt->NextSiblingElement("BehaviorTree"))
  {
    const char * id = bt->Attribute("ID");
    if (id) {
      info.behavior_tree_ids.emplace_back(id);
    }
  }

  // First try to get main_tree_to_execute attribute
  const char * main_attr = root->Attribute("main_tree_to_execute");
  if (main_attr) {
    info.main_id = main_attr;
  }

  // If main_tree_to_execute attribute is not set, we first check the number of BehaviorTree tags
  if (info.main_id.empty()) {
    // If only one BehaviorTree tag is found, we can use that as the main ID
    // If multiple are found, we throw an error since we don't know
    // which one to use as the main tree
    if (info.behavior_tree_ids.size() == 1) {
      info.main_id = info.behavior_tree_ids[0];
    } else if (info.behavior_tree_ids.size() > 1) {
      throw std::runtime_error(
              "Multiple BehaviorTree elements found in " + filename +
              " but no main_tree_to_execute attribute specified. Unable to determine main tree.");
    }
  }

  return info;
}

BT::Tree
BehaviorTreeEngine::createTree(
  const std::string & tree_id,
  BT::Blackboard::Ptr blackboard)
{
  return factory_.createTree(tree_id, blackboard);
}

/// @brief Register a tree from an XML file and return the tree
void BehaviorTreeEngine::registerTreeFromFile(
  const std::string & file_path)
{
  factory_.registerBehaviorTreeFromFile(file_path);
}

void
BehaviorTreeEngine::addGrootMonitoring(
  BT::Tree * tree,
  uint16_t server_port)
{
  // This logger publish status changes using Groot2
  groot_monitor_ = std::make_unique<BT::Groot2Publisher>(*tree, server_port);

  // Register common types JSON definitions
  BT::RegisterJsonDefinition<builtin_interfaces::msg::Time>();
  BT::RegisterJsonDefinition<std_msgs::msg::Header>();
}

void
BehaviorTreeEngine::resetGrootMonitor()
{
  if (groot_monitor_) {
    groot_monitor_.reset();
  }
}

// In order to re-run a Behavior Tree, we must be able to reset all nodes to the initial state
void
BehaviorTreeEngine::haltAllActions(BT::Tree & tree)
{
  // this halt signal should propagate through the entire tree.
  tree.haltTree();
}

}  // namespace nav2_behavior_tree
