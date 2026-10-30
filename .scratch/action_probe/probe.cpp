// Can rclcpp resolve the typesupports an action is made of, from the type name
// alone? Everything else in callService() depends on the answer.
#include <iostream>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp/typesupport_helpers.hpp>
#include <rosidl_runtime_c/message_type_support_struct.h>

int main()
{
  const std::string service_type = "action_tutorials_interfaces/action/Fibonacci_SendGoal";
  try {
    auto library = rclcpp::get_typesupport_library(service_type, "rosidl_typesupport_cpp");
    const auto * service = rclcpp::get_service_typesupport_handle(
      service_type, "rosidl_typesupport_cpp", *library);
    std::cout << "service handle: " << (service != nullptr ? "ok" : "null");
    if (service != nullptr) {
      std::cout << " request=" << service->request_typesupport->typesupport_identifier
                << " response=" << service->response_typesupport->typesupport_identifier;
    }
    std::cout << std::endl;
  } catch (const std::exception & error) {
    std::cout << "service handle FAILED: " << error.what() << std::endl;
  }

  const char * messages[] = {
    "action_tutorials_interfaces/action/Fibonacci_Goal",
    "action_tutorials_interfaces/action/Fibonacci_Result",
    "action_tutorials_interfaces/action/Fibonacci_Feedback",
    "action_tutorials_interfaces/action/Fibonacci_FeedbackMessage",
    "action_msgs/srv/SendGoal_Request",
    "action_msgs/srv/SendGoal_Response",
    "action_msgs/srv/GetResult_Request",
    "action_msgs/srv/CancelGoal_Request",
    "action_msgs/msg/GoalStatusArray",
  };
  for (const char * type : messages) {
    try {
      auto library = rclcpp::get_typesupport_library(type, "rosidl_typesupport_cpp");
      const auto * handle = rclcpp::get_message_typesupport_handle(
        type, "rosidl_typesupport_cpp", *library);
      std::cout << type << " -> " << (handle != nullptr ? handle->typesupport_identifier : "NULL")
                << std::endl;
    } catch (const std::exception & error) {
      std::cout << type << " -> FAILED: " << error.what() << std::endl;
    }
  }
  return 0;
}
