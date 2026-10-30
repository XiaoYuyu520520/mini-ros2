#ifndef RCL_INTERFACES_SRV_GETPARAMETERS_HPP_
#define RCL_INTERFACES_SRV_GETPARAMETERS_HPP_

#include "rcl_interfaces/msg/parameter_value.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace srv
{

struct GetParameters
{
  static constexpr const char * ros2_service_type = "rcl_interfaces/srv/GetParameters";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "rcl_interfaces/srv/GetParameters_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _names_type = std::vector<std::string>;
      std::vector<std::string> names;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(names);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(names);
      }

      bool operator==(const Request & other) const
      {
        return names == other.names;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "rcl_interfaces/srv/GetParameters_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _values_type = std::vector<::rcl_interfaces::msg::ParameterValue>;
      std::vector<::rcl_interfaces::msg::ParameterValue> values;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(values);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(values);
      }

      bool operator==(const Response & other) const
      {
        return values == other.values;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_SRV_GETPARAMETERS_HPP_
