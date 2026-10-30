#ifndef RCLCPP_LIFECYCLE__STATE_HPP_
#define RCLCPP_LIFECYCLE__STATE_HPP_

#include <cstdint>
#include <string>

#include "lifecycle_msgs/msg/state.hpp"

namespace rclcpp_lifecycle
{

class State
{
public:
  State() = default;

  State(uint8_t id, const std::string & label)
  : id_(id), label_(label)
  {
  }

  uint8_t id() const {return id_;}

  const std::string & label() const {return label_;}

private:
  uint8_t id_ = lifecycle_msgs::msg::State::PRIMARY_STATE_UNKNOWN;
  std::string label_ = "unknown";
};

}  // namespace rclcpp_lifecycle

#endif  // RCLCPP_LIFECYCLE__STATE_HPP_
