#ifndef GEOGRAPHIC_MSGS_SRV_UPDATEGEOGRAPHICMAP_HPP_
#define GEOGRAPHIC_MSGS_SRV_UPDATEGEOGRAPHICMAP_HPP_

#include "geographic_msgs/msg/geographic_map_changes.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geographic_msgs
{
namespace srv
{

struct UpdateGeographicMap
{
  static constexpr const char * ros2_service_type = "geographic_msgs/srv/UpdateGeographicMap";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "geographic_msgs/srv/UpdateGeographicMap_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _updates_type = ::geographic_msgs::msg::GeographicMapChanges;
      ::geographic_msgs::msg::GeographicMapChanges updates;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(updates);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(updates);
      }

      bool operator==(const Request & other) const
      {
        return updates == other.updates;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "geographic_msgs/srv/UpdateGeographicMap_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _success_type = bool;
      bool success = false;
      using _status_type = std::string;
      std::string status;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(success);
        ar(status);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(success);
        ar(status);
      }

      bool operator==(const Response & other) const
      {
        return success == other.success &&
          status == other.status;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_SRV_UPDATEGEOGRAPHICMAP_HPP_
