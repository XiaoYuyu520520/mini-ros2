#ifndef RCL_INTERFACES_SRV_SETLOGGERLEVELS_HPP_
#define RCL_INTERFACES_SRV_SETLOGGERLEVELS_HPP_

#include "rcl_interfaces/msg/logger_level.hpp"
#include "rcl_interfaces/msg/set_logger_levels_result.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace srv
{

struct SetLoggerLevels
{
  static constexpr const char * ros2_service_type = "rcl_interfaces/srv/SetLoggerLevels";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "rcl_interfaces/srv/SetLoggerLevels_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
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

      bool operator==(const Request & other) const
      {
        return levels == other.levels;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "rcl_interfaces/srv/SetLoggerLevels_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _results_type = std::vector<::rcl_interfaces::msg::SetLoggerLevelsResult>;
      std::vector<::rcl_interfaces::msg::SetLoggerLevelsResult> results;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(results);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(results);
      }

      bool operator==(const Response & other) const
      {
        return results == other.results;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_SRV_SETLOGGERLEVELS_HPP_
