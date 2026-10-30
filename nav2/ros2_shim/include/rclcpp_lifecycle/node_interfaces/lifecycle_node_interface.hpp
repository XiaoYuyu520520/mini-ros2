#ifndef RCLCPP_LIFECYCLE__NODE_INTERFACES__LIFECYCLE_NODE_INTERFACE_HPP_
#define RCLCPP_LIFECYCLE__NODE_INTERFACES__LIFECYCLE_NODE_INTERFACE_HPP_

#include "rclcpp_lifecycle/state.hpp"

namespace rclcpp_lifecycle
{
namespace node_interfaces
{

class LifecycleNodeInterface
{
public:
// <windows.h> (pulled in transitively through ros2_shim/graph.hpp) defines the
// wingdi.h macro ERROR, which otherwise mangles the enumerator below.
#ifdef ERROR
#pragma push_macro("ERROR")
#undef ERROR
#define ROS2_SHIM_RESTORE_ERROR_MACRO
#endif
  enum class CallbackReturn
  {
    SUCCESS = 0,
    FAILURE = 1,
    ERROR = 2,
  };
#ifdef ROS2_SHIM_RESTORE_ERROR_MACRO
#pragma pop_macro("ERROR")
#undef ROS2_SHIM_RESTORE_ERROR_MACRO
#endif

  virtual ~LifecycleNodeInterface() = default;

  virtual CallbackReturn on_configure(const State &) {return CallbackReturn::SUCCESS;}
  virtual CallbackReturn on_activate(const State &) {return CallbackReturn::SUCCESS;}
  virtual CallbackReturn on_deactivate(const State &) {return CallbackReturn::SUCCESS;}
  virtual CallbackReturn on_cleanup(const State &) {return CallbackReturn::SUCCESS;}
  virtual CallbackReturn on_shutdown(const State &) {return CallbackReturn::SUCCESS;}
  virtual CallbackReturn on_error(const State &) {return CallbackReturn::SUCCESS;}
};

}  // namespace node_interfaces
}  // namespace rclcpp_lifecycle

#endif  // RCLCPP_LIFECYCLE__NODE_INTERFACES__LIFECYCLE_NODE_INTERFACE_HPP_
