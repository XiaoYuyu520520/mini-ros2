#ifndef RCLCPP__PARAMETER_EVENT_HANDLER_HPP_
#define RCLCPP__PARAMETER_EVENT_HANDLER_HPP_

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "rclcpp/detail/parameter_event_hub.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/node.hpp"
#include "rclcpp/parameter.hpp"
#include "rclcpp/qos.hpp"

namespace rclcpp
{

/// The registration behind one `add_parameter_callback` call.
///
/// Dropping the last reference unregisters the callback. Upstream leaves that
/// to an explicit `remove_parameter_callback`, but the callback is delivered
/// here into whatever object registered it - Smac hands over a lambda capturing
/// `this` - so a registration that outlived its owner would be a call into a
/// destroyed object. Unguarding on destruction is the one behaviour that cannot
/// do that, and calling `remove_parameter_callback` still works and is
/// idempotent.
class ParameterCallbackHandle
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(ParameterCallbackHandle)

  ParameterCallbackHandle(
    std::size_t id, std::string node_name, std::string parameter_name)
  : id_(id), node_name_(std::move(node_name)), parameter_name_(std::move(parameter_name))
  {
  }

  ~ParameterCallbackHandle()
  {
    detail::ParameterEventHub::instance().remove(id_);
  }

  ParameterCallbackHandle(const ParameterCallbackHandle &) = delete;
  ParameterCallbackHandle & operator=(const ParameterCallbackHandle &) = delete;

  std::size_t id() const {return id_;}
  const std::string & node_name() const {return node_name_;}
  const std::string & parameter_name() const {return parameter_name_;}

private:
  std::size_t id_;
  std::string node_name_;
  std::string parameter_name_;
};

/// Watch another node's parameters, by name.
///
/// Upstream this is a `/parameter_events` subscription; the shim delivers the
/// same events in process, from inside `Node::set_parameters`, so `node` is
/// taken for API compatibility rather than to subscribe from. Delivery is
/// documented in `rclcpp::detail::ParameterEventHub`.
class ParameterEventHandler
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(ParameterEventHandler)

  using ParameterCallback = std::function<void(const rclcpp::Parameter &)>;

  template<typename NodeT>
  explicit ParameterEventHandler(NodeT node, const rclcpp::QoS & qos = rclcpp::QoS(1000))
  : node_(node)
  {
    (void)qos;
  }

  /// Calls `callback` whenever the node named `node_name` sets `parameter_name`
  /// successfully. An empty `node_name` watches every node in the process.
  ParameterCallbackHandle::SharedPtr add_parameter_callback(
    const std::string & parameter_name, ParameterCallback callback,
    const std::string & node_name = "")
  {
    const std::size_t id = detail::ParameterEventHub::instance().add(
      node_name, parameter_name, std::move(callback));
    return std::make_shared<ParameterCallbackHandle>(id, node_name, parameter_name);
  }

  bool remove_parameter_callback(const ParameterCallbackHandle::SharedPtr & handle)
  {
    return handle != nullptr && detail::ParameterEventHub::instance().remove(handle->id());
  }

private:
  // Held only to keep the constructor honest about what it accepts: the events
  // come from the hub, not from this node. `weak_ptr<void>` takes a
  // `shared_ptr` to any node type, including the templated lifecycle nodes.
  std::weak_ptr<void> node_;
};

}  // namespace rclcpp

#endif  // RCLCPP__PARAMETER_EVENT_HANDLER_HPP_
