#ifndef RCLCPP__VERSION_H_
#define RCLCPP__VERSION_H_

#define RCLCPP_VERSION_MAJOR 28
#define RCLCPP_VERSION_MINOR 0
#define RCLCPP_VERSION_PATCH 0
#define RCLCPP_VERSION "28.0.0"

#define RCLCPP_VERSION_GTE(major, minor, patch) \
  ((RCLCPP_VERSION_MAJOR > (major)) || \
  (RCLCPP_VERSION_MAJOR == (major) && RCLCPP_VERSION_MINOR > (minor)) || \
  (RCLCPP_VERSION_MAJOR == (major) && RCLCPP_VERSION_MINOR == (minor) && \
  RCLCPP_VERSION_PATCH >= (patch)))

#endif  // RCLCPP__VERSION_H_
