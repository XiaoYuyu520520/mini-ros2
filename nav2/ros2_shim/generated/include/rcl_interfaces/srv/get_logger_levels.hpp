#ifndef RCL_INTERFACES_SRV_GETLOGGERLEVELS_HPP_
#define RCL_INTERFACES_SRV_GETLOGGERLEVELS_HPP_

#include "rcl_interfaces/msg/logger_level.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace srv
{

struct GetLoggerLevels
{
  static constexpr const char * ros2_service_type = "rcl_interfaces/srv/GetLoggerLevels";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "rcl_interfaces/srv/GetLoggerLevels_Request";
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
      static constexpr const char * ros2_message_type = "rcl_interfaces/srv/GetLoggerLevels_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _levels_type = std::vector<::rcl_interfaces::msg::LoggerLevel>;
      std::vector<::rcl_interfaces::msg::LoggerLevel> levels;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(levels);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(levels);
      }

      bool operator==(const Response & other) const
      {
        return levels == other.levels;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_SRV_GETLOGGERLEVELS_HPP_
