#ifndef RCLCPP__PARAMETER_SERVICE_HPP_
#define RCLCPP__PARAMETER_SERVICE_HPP_

// The six services a node answers parameter questions on - `~/get_parameters`,
// `~/get_parameter_types`, `~/set_parameters`, `~/set_parameters_atomically`,
// `~/describe_parameters` and `~/list_parameters` - which is what `ros2 param`
// and every other introspection tool talks to.
//
// Until now this header was a stub: the shim could `declare`, `get` and `set`
// its own parameters, and the messages were announced inside the process
// (`detail::ParameterEventHub`), but *no other process* could read or write a
// running node's parameters. Every `ros2 param` verb and every remote tool
// needs exactly these six, so they are the first thing to build.
//
// Declaration and definition are split across two headers: the handlers below
// are defined at the bottom of `rclcpp/node.hpp`, because they need a complete
// `Node` and the shim is header-only - the seam an upstream `.cpp` would fill,
// filled by the include order instead. A user of this header includes
// `rclcpp/rclcpp.hpp` (or `rclcpp/node.hpp`), as upstream's users do; the
// declaration alone is here so that `Node` can name the member that owns it
// without a circular include.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "rcl_interfaces/msg/set_parameters_result.hpp"
#include "rcl_interfaces/srv/describe_parameters.hpp"
#include "rcl_interfaces/srv/get_parameter_types.hpp"
#include "rcl_interfaces/srv/get_parameters.hpp"
#include "rcl_interfaces/srv/list_parameters.hpp"
#include "rcl_interfaces/srv/set_parameters.hpp"
#include "rcl_interfaces/srv/set_parameters_atomically.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/parameter.hpp"
#include "ros2_shim/parameter_support.hpp"

namespace rclcpp
{

class Node;

/// The parameter services of one node.
///
/// Constructing this puts the six services into the node's service list and
/// into the shim graph, so `ros2 service list` and a client's
/// `wait_for_service()` both see them; the node owns them for its lifetime.
/// The semantics the handlers implement are the ones
/// `rcl_interfaces/srv/*.srv` specifies, with two deliberate choices:
///
///   * A name the node does not have declared cannot be *created* through a
///     service. `set_parameters` answers `successful = false` with
///     `parameter '<name>' has not been declared`, and nothing is stored. The
///     in-process C++ API (`Node::set_parameters`) stays as permissive as it
///     was - nav2's own code is what declares its parameters, and a tool
///     setting a name nobody declared is a typo far more often than it is a
///     feature.
///   * A descriptor's `read_only` flag is enforced here for the same reason:
///     this is the boundary a stranger can reach.
class ParameterService
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(ParameterService)

  explicit ParameterService(Node * node);

  Node * get_node() const {return node_;}

private:
  /// Answers in request order, one value per requested name;
  /// `PARAMETER_NOT_SET` for a name that is undeclared or declared without a
  /// value, which is what `GetParameters.srv` specifies.
  void handle_get_parameters(
    std::shared_ptr<rcl_interfaces::srv::GetParameters::Request> request,
    std::shared_ptr<rcl_interfaces::srv::GetParameters::Response> response);

  /// `GetParameterTypes.srv`: the same order, the `ParameterType` enum value
  /// per name, `PARAMETER_NOT_SET` for anything not currently set.
  void handle_get_parameter_types(
    std::shared_ptr<rcl_interfaces::srv::GetParameterTypes::Request> request,
    std::shared_ptr<rcl_interfaces::srv::GetParameterTypes::Response> response);

  /// `SetParameters.srv`: one result per requested parameter, in order.
  ///
  /// A rejected name is rejected on its own - the other parameters of the call
  /// are still set and still report their own result - which is the difference
  /// between this and `set_parameters_atomically`.
  void handle_set_parameters(
    std::shared_ptr<rcl_interfaces::srv::SetParameters::Request> request,
    std::shared_ptr<rcl_interfaces::srv::SetParameters::Response> response);

  /// `SetParametersAtomically.srv`: either every parameter in the request is
  /// set or none is, and the answer is the one result that says which.
  void handle_set_parameters_atomically(
    std::shared_ptr<rcl_interfaces::srv::SetParametersAtomically::Request> request,
    std::shared_ptr<rcl_interfaces::srv::SetParametersAtomically::Response> response);

  /// `DescribeParameters.srv`: one descriptor per requested name, in order.
  ///
  /// An undeclared name answers with a `PARAMETER_NOT_SET` descriptor whose
  /// `name` is filled in, rather than failing the whole call: the service
  /// promises one descriptor per name asked about, and a tool that asked for
  /// five names can then say which of them it could not find.
  void handle_describe_parameters(
    std::shared_ptr<rcl_interfaces::srv::DescribeParameters::Request> request,
    std::shared_ptr<rcl_interfaces::srv::DescribeParameters::Response> response);

  /// `ListParameters.srv`.
  ///
  /// A name goes into `result.names` when it lies under one of the requested
  /// prefixes - all of them when the request names none - with at most `depth`
  /// separator (`.`) separated segments below that prefix. A name further down
  /// than that is not listed; instead its first `depth` segments below the
  /// prefix are collected into `result.prefixes`, trailing separator included,
  /// so that a prefix can be handed straight back as the next request's
  /// prefix. `depth = DEPTH_RECURSIVE` (0) lists everything under the
  /// prefixes, and both lists are sorted and free of duplicates - which is
  /// what makes them usable as a tree walk for a tool.
  void handle_list_parameters(
    std::shared_ptr<rcl_interfaces::srv::ListParameters::Request> request,
    std::shared_ptr<rcl_interfaces::srv::ListParameters::Response> response);

  /// The reason a name cannot be set, or a successful result when it can.
  ///
  /// Three answers are the service's own rather than `Node::set_parameters`'s,
  /// because this is the boundary a stranger reaches: an undeclared name is not
  /// created, a `read_only` parameter is not written, and a value whose type
  /// differs from the declared one is refused - the in-process API would take
  /// all three, and a node whose parameter changed type under it is a node
  /// whose next `get_parameter<double>` throws.
  rcl_interfaces::msg::SetParametersResult check_settable(
    const std::string & name, const rclcpp::ParameterValue & value) const;

  /// Creates one of the six on `node` - the node's fully qualified name plus
  /// `suffix` - and points it at one of the handlers above.
  ///
  /// The name is spelled out instead of `~/suffix`, which would be the
  /// upstream spelling: `Node::plain_topic_name` expands `~` against the node
  /// *name* alone, so a namespaced node would lose its namespace and answer on
  /// a service name no client asks for.
  template<class ServiceT, class Handler>
  void add_service(Node * node, const std::string & suffix, Handler handler);

  Node * node_;
};

}  // namespace rclcpp

#endif  // RCLCPP__PARAMETER_SERVICE_HPP_
