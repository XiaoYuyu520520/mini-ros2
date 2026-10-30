#ifndef NAV2_MSGS_SRV_REMOVEEXCLUSIONZONE_HPP_
#define NAV2_MSGS_SRV_REMOVEEXCLUSIONZONE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace srv
{

struct RemoveExclusionZone
{
  static constexpr const char * ros2_service_type = "nav2_msgs/srv/RemoveExclusionZone";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/RemoveExclusionZone_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _zone_name_type = std::string;
      std::string zone_name;
      using _remove_all_type = bool;
      bool remove_all = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(zone_name);
        ar(remove_all);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(zone_name);
        ar(remove_all);
      }

      bool operator==(const Request & other) const
      {
        return zone_name == other.zone_name &&
          remove_all == other.remove_all;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/RemoveExclusionZone_Response";
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

#endif  // NAV2_MSGS_SRV_REMOVEEXCLUSIONZONE_HPP_
