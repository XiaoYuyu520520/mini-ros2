#ifndef NAV2_MSGS_SRV_ADDEXCLUSIONZONE_HPP_
#define NAV2_MSGS_SRV_ADDEXCLUSIONZONE_HPP_

#include "nav2_msgs/msg/exclusion_zone_description.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace srv
{

struct AddExclusionZone
{
  static constexpr const char * ros2_service_type = "nav2_msgs/srv/AddExclusionZone";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/AddExclusionZone_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _zone_type = ::nav2_msgs::msg::ExclusionZoneDescription;
      ::nav2_msgs::msg::ExclusionZoneDescription zone;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(zone);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(zone);
      }

      bool operator==(const Request & other) const
      {
        return zone == other.zone;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/AddExclusionZone_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _success_type = bool;
      bool success = false;
      using _message_type = std::string;
      std::string message;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(success);
        ar(message);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(success);
        ar(message);
      }

      bool operator==(const Response & other) const
      {
        return success == other.success &&
          message == other.message;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_SRV_ADDEXCLUSIONZONE_HPP_
