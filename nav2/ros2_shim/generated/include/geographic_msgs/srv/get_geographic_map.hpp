#ifndef GEOGRAPHIC_MSGS_SRV_GETGEOGRAPHICMAP_HPP_
#define GEOGRAPHIC_MSGS_SRV_GETGEOGRAPHICMAP_HPP_

#include "geographic_msgs/msg/bounding_box.hpp"
#include "geographic_msgs/msg/geographic_map.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geographic_msgs
{
namespace srv
{

struct GetGeographicMap
{
  static constexpr const char * ros2_service_type = "geographic_msgs/srv/GetGeographicMap";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "geographic_msgs/srv/GetGeographicMap_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _url_type = std::string;
      std::string url;
      using _bounds_type = ::geographic_msgs::msg::BoundingBox;
      ::geographic_msgs::msg::BoundingBox bounds;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(url);
        ar(bounds);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(url);
        ar(bounds);
      }

      bool operator==(const Request & other) const
      {
        return url == other.url &&
          bounds == other.bounds;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "geographic_msgs/srv/GetGeographicMap_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _success_type = bool;
      bool success = false;
      using _status_type = std::string;
      std::string status;
      using _map_type = ::geographic_msgs::msg::GeographicMap;
      ::geographic_msgs::msg::GeographicMap map;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(success);
        ar(status);
        ar(map);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(success);
        ar(status);
        ar(map);
      }

      bool operator==(const Response & other) const
      {
        return success == other.success &&
          status == other.status &&
          map == other.map;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_SRV_GETGEOGRAPHICMAP_HPP_
