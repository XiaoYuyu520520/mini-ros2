#ifndef RCL_INTERFACES_SRV_LISTPARAMETERS_HPP_
#define RCL_INTERFACES_SRV_LISTPARAMETERS_HPP_

#include "rcl_interfaces/msg/list_parameters_result.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace srv
{

struct ListParameters
{
  static constexpr const char * ros2_service_type = "rcl_interfaces/srv/ListParameters";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "rcl_interfaces/srv/ListParameters_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      static constexpr uint64_t DEPTH_RECURSIVE = 0;

      using _prefixes_type = std::vector<std::string>;
      std::vector<std::string> prefixes;
      using _depth_type = uint64_t;
      uint64_t depth = 0;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(prefixes);
        ar(depth);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(prefixes);
        ar(depth);
      }

      bool operator==(const Request & other) const
      {
        return prefixes == other.prefixes &&
          depth == other.depth;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "rcl_interfaces/srv/ListParameters_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _result_type = ::rcl_interfaces::msg::ListParametersResult;
      ::rcl_interfaces::msg::ListParametersResult result;

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

#endif  // RCL_INTERFACES_SRV_LISTPARAMETERS_HPP_
