#ifndef RCLCPP__CALLBACK_GROUP_HPP_
#define RCLCPP__CALLBACK_GROUP_HPP_

#include <memory>

#include "rclcpp/macros.hpp"

namespace rclcpp
{

enum class CallbackGroupType
{
  MutuallyExclusive,
  Reentrant,
};

class CallbackGroup
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(CallbackGroup)

  explicit CallbackGroup(
    CallbackGroupType type = CallbackGroupType::MutuallyExclusive,
    bool automatically_add_to_executor = true)
  : type_(type), automatically_add_to_executor_(automatically_add_to_executor)
  {
  }

  CallbackGroupType type() const {return type_;}
  CallbackGroupType get_type() const {return type_;}

  // Upstream's switch, and the one the ported packages lean on: a group created
  // with `false` belongs to nobody until an executor is handed it explicitly, so
  // a plain spin of the node must leave its entities alone.
  //
  // nav2 creates every one of its groups that way, and two of them make the
  // difference visible. `nav2::SimpleActionServer` gives its action server such
  // a group and a thread of its own, and `on_cleanup` destroys that server: an
  // executor that drove the whole node would run `~/change_state` on that
  // thread, the cleanup would tear the server down there, and `~NodeThread`
  // would join the thread it is already on - "Resource deadlock avoided".
  // `nav2::ServiceClient` with an internal executor waits on a group of its own,
  // and an executor that drove the whole node would instead poll every
  // subscription the node has, from whichever thread made the call.
  bool automatically_add_to_executor_with_node() const
  {
    return automatically_add_to_executor_;
  }

  void set_automatically_add_to_executor_with_node(bool value)
  {
    automatically_add_to_executor_ = value;
  }

private:
  CallbackGroupType type_;
  bool automatically_add_to_executor_;
};

namespace callback_groups
{
using CallbackGroup = ::rclcpp::CallbackGroup;
}  // namespace callback_groups

}  // namespace rclcpp

#endif  // RCLCPP__CALLBACK_GROUP_HPP_
