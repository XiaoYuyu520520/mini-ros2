#ifndef RCL_INTERFACES_SRV_DESCRIBEPARAMETERS_HPP_
#define RCL_INTERFACES_SRV_DESCRIBEPARAMETERS_HPP_

#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace srv
{

struct DescribeParameters
{
  static constexpr const char * ros2_service_type = "rcl_interfaces/srv/DescribeParameters";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "rcl_interfaces/srv/DescribeParameters_Request";
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
      static constexpr const char * ros2_message_type = "rcl_interfaces/srv/DescribeParameters_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _descriptors_type = std::vector<::rcl_interfaces::msg::ParameterDescriptor>;
      std::vector<::rcl_interfaces::msg::ParameterDescriptor> descriptors;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(descriptors);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(descriptors);
      }

      bool operator==(const Response & other) const
      {
        return descriptors == other.descriptors;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_SRV_DESCRIBEPARAMETERS_HPP_
