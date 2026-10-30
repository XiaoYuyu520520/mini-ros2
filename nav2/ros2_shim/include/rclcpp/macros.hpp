#ifndef RCLCPP__MACROS_HPP_
#define RCLCPP__MACROS_HPP_

#include <memory>
#include <utility>

#define RCLCPP_SMART_PTR_DEFINITIONS(...) \
  using SharedPtr = std::shared_ptr<__VA_ARGS__>; \
  using WeakPtr = std::weak_ptr<__VA_ARGS__>; \
  using UniquePtr = std::unique_ptr<__VA_ARGS__>; \
  using ConstSharedPtr = std::shared_ptr<const __VA_ARGS__>; \
  template<typename... ArgsT> \
  static SharedPtr make_shared(ArgsT &&... args) \
  {return std::make_shared<__VA_ARGS__>(std::forward<ArgsT>(args)...);}

#define RCLCPP_SMART_PTR_DEFINITIONS_NOT_COPYABLE(...) \
  RCLCPP_SMART_PTR_DEFINITIONS(__VA_ARGS__) \
  __VA_ARGS__(const __VA_ARGS__ &) = delete; \
  __VA_ARGS__ & operator=(const __VA_ARGS__ &) = delete;

#define RCLCPP_SMART_PTR_ALIASES_ONLY(...) \
  using SharedPtr = std::shared_ptr<__VA_ARGS__>; \
  using WeakPtr = std::weak_ptr<__VA_ARGS__>; \
  using UniquePtr = std::unique_ptr<__VA_ARGS__>; \
  using ConstSharedPtr = std::shared_ptr<const __VA_ARGS__>;

#define RCLCPP_PUBLIC
#define RCLCPP_LOCAL
#define RCLCPP_EXPORT

#endif  // RCLCPP__MACROS_HPP_
