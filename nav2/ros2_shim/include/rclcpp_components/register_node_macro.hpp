#ifndef RCLCPP_COMPONENTS__REGISTER_NODE_MACRO_HPP_
#define RCLCPP_COMPONENTS__REGISTER_NODE_MACRO_HPP_

// Component registration is a build/plugin concept that is unnecessary when
// nodes are linked directly into the shared-memory executable.
#define RCLCPP_COMPONENTS_REGISTER_NODE(...) \
  namespace \
  { \
  [[maybe_unused]] const bool rclcpp_components_registered_##__LINE__ = true; \
  }

#endif  // RCLCPP_COMPONENTS__REGISTER_NODE_MACRO_HPP_
