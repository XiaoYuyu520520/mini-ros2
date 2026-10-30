#ifndef RCLCPP__DETAIL__PARAMETER_EVENT_HUB_HPP_
#define RCLCPP__DETAIL__PARAMETER_EVENT_HUB_HPP_

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "rclcpp/parameter.hpp"

namespace rclcpp
{
namespace detail
{

/// In-process delivery of "this node's parameter changed" to whoever asked.
///
/// Upstream this is `/parameter_events`: a topic every node publishes to after a
/// successful `set_parameters`, and `ParameterEventHandler` subscribes to it.
/// A node now publishes that topic here too (`Node::publish_parameter_event`),
/// but delivery to `ParameterEventHandler` still happens through this hub, for
/// two reasons: the handler is this shim's own class and is handed a node rather
/// than a subscription, and an in-process call reaches a listener whether or not
/// anything is spinning the node the callback lives on - a topic would need a
/// spin between the set and the callback, which the ported Smac planner does not
/// have. The hub is called at the one point a node's parameters actually change
/// (`rclcpp::Node::set_parameters`), and only for sets that were applied, which
/// is the same rule the topic is published under.
///
/// A listener names the node it cares about; an empty name means "any node".
/// Names are compared with the leading slash stripped, because `/global_costmap`
/// and `global_costmap` are one node and nav2 builds the string in more than one
/// way. That is a normalisation only - it does not make
/// `global_costmap/global_costmap` match `global_costmap`, because upstream does
/// not either (see the note in `smac_planner_hybrid_impl.hpp`, where that string
/// is what nav2 hardcodes).
class ParameterEventHub
{
public:
  using Callback = std::function<void(const rclcpp::Parameter &)>;

  static ParameterEventHub & instance()
  {
    static ParameterEventHub hub;
    return hub;
  }

  ParameterEventHub(const ParameterEventHub &) = delete;
  ParameterEventHub & operator=(const ParameterEventHub &) = delete;

  std::size_t add(
    const std::string & node_name, const std::string & parameter_name, Callback callback)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t id = next_id_++;
    listeners_.push_back(Listener{id, node_name, parameter_name, std::move(callback)});
    return id;
  }

  bool remove(std::size_t id)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
      if (it->id == id) {
        listeners_.erase(it);
        return true;
      }
    }
    return false;
  }

  /// Calls every callback registered for `node_name` on a name in `parameters`.
  ///
  /// The callbacks are copied out under the lock and run without it. A callback
  /// is free to read the same node's parameters (Smac's does) and to add or
  /// remove listeners, and either would deadlock against a hub lock held across
  /// the call.
  void notify(
    const std::string & node_name, const std::vector<rclcpp::Parameter> & parameters)
  {
    std::vector<std::pair<Callback, const rclcpp::Parameter *>> due;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (const auto & listener : listeners_) {
        if (!same_node(listener.node_name, node_name)) {
          continue;
        }
        for (const auto & parameter : parameters) {
          if (parameter.get_name() == listener.parameter_name) {
            due.emplace_back(listener.callback, &parameter);
          }
        }
      }
    }
    for (const auto & entry : due) {
      entry.first(*entry.second);
    }
  }

  /// For tests and for `ros2_shim` diagnostics: how many registrations are live.
  std::size_t size() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return listeners_.size();
  }

private:
  ParameterEventHub() = default;

  struct Listener
  {
    std::size_t id;
    std::string node_name;
    std::string parameter_name;
    Callback callback;
  };

  static bool same_node(const std::string & wanted, const std::string & actual)
  {
    if (wanted.empty() || wanted == actual) {
      return true;
    }
    return withoutLeadingSlash(wanted) == withoutLeadingSlash(actual);
  }

  static std::string withoutLeadingSlash(const std::string & name)
  {
    if (!name.empty() && name.front() == '/') {
      return name.substr(1);
    }
    return name;
  }

  mutable std::mutex mutex_;
  std::vector<Listener> listeners_;
  std::size_t next_id_{1};
};

}  // namespace detail
}  // namespace rclcpp

#endif  // RCLCPP__DETAIL__PARAMETER_EVENT_HUB_HPP_
