#ifndef RCLCPP__PARAMETER_EVENTS_FILTER_HPP_
#define RCLCPP__PARAMETER_EVENTS_FILTER_HPP_

#include <string>
#include <utility>
#include <vector>

#include "rcl_interfaces/msg/parameter_event.hpp"

namespace rclcpp
{

/// Sort the parameters carried by a `/parameter_events` message into the three
/// things a node can have done to them, keeping only the ones the caller asked
/// about.
///
/// `rclcpp::Node` publishes the stream this sorts (`Node::publish_parameter_event`),
/// so it is a real message and not a shape waiting for one. Nothing in the
/// ported code constructs the filter yet - the costmap layers only include the
/// header, upstream included it for a parameter-event subscription they no
/// longer open - but a subscriber that does get one has the three lists it
/// expects: `new_parameters` from a declaration, `changed_parameters` from an
/// applied set, and `deleted_parameters` never, because the shim has no
/// `undeclare_parameter`.
class ParameterEventsFilter
{
public:
  enum class EventType
  {
    NEW,
    CHANGED,
    DELETED,
  };

  using EventPair = std::pair<EventType, const rcl_interfaces::msg::Parameter *>;

  ParameterEventsFilter() = default;

  ParameterEventsFilter(
    const rcl_interfaces::msg::ParameterEvent::SharedPtr event,
    const std::vector<std::string> & names,
    const std::vector<EventType> & types)
  {
    if (!event) {
      return;
    }
    collect(event->new_parameters, EventType::NEW, names, types);
    collect(event->changed_parameters, EventType::CHANGED, names, types);
    collect(event->deleted_parameters, EventType::DELETED, names, types);
  }

  std::vector<EventPair> get_events() const
  {
    return events_;
  }

private:
  static bool wanted(const std::string & name, const std::vector<std::string> & names)
  {
    for (const auto & candidate : names) {
      if (candidate == name) {
        return true;
      }
    }
    return false;
  }

  static bool wanted(EventType type, const std::vector<EventType> & types)
  {
    for (const auto & candidate : types) {
      if (candidate == type) {
        return true;
      }
    }
    return false;
  }

  void collect(
    const std::vector<rcl_interfaces::msg::Parameter> & parameters, EventType type,
    const std::vector<std::string> & names, const std::vector<EventType> & types)
  {
    if (!wanted(type, types)) {
      return;
    }
    for (const auto & parameter : parameters) {
      if (wanted(parameter.name, names)) {
        events_.emplace_back(type, &parameter);
      }
    }
  }

  std::vector<EventPair> events_;
};

}  // namespace rclcpp

#endif  // RCLCPP__PARAMETER_EVENTS_FILTER_HPP_
