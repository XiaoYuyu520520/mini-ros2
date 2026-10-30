#ifndef RCL_INTERFACES_SRV_SETPARAMETERSATOMICALLY_HPP_
#define RCL_INTERFACES_SRV_SETPARAMETERSATOMICALLY_HPP_

#include "rcl_interfaces/msg/parameter.hpp"
#include "rcl_interfaces/msg/set_parameters_result.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace srv
{

struct SetParametersAtomically
{
  static constexpr const char * ros2_service_type = "rcl_interfaces/srv/SetParametersAtomically";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "rcl_interfaces/srv/SetParametersAtomically_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _parameters_type = std::vector<::rcl_interfaces::msg::Parameter>;
      std::vector<::rcl_interfaces::msg::Parameter> parameters;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(parameters);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(parameters);
      }

      bool operator==(const Request & other) const
      {
        return parameters == other.parameters;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "rcl_interfaces/srv/SetParametersAtomically_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _result_type = ::rcl_interfaces::msg::SetParametersResult;
      ::rcl_interfaces::msg::SetParametersResult result;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(result);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(result);
      }

      bool operator==(const Response & other) const
      {
        return result == other.result;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_SRV_SETPARAMETERSATOMICALLY_HPP_
