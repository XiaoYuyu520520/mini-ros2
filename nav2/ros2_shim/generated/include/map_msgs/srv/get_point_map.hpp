#ifndef MAP_MSGS_SRV_GETPOINTMAP_HPP_
#define MAP_MSGS_SRV_GETPOINTMAP_HPP_

#include "sensor_msgs/msg/point_cloud2.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace map_msgs
{
namespace srv
{

struct GetPointMap
{
  static constexpr const char * ros2_service_type = "map_msgs/srv/GetPointMap";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "map_msgs/srv/GetPointMap_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;

      template <class Archive>
      void visit(Archive & ar)
      {
        (void)ar;
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        (void)ar;
      }

      bool operator==(const Request & other) const
      {
        (void)other;
        return true;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "map_msgs/srv/GetPointMap_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _map_type = ::sensor_msgs::msg::PointCloud2;
      ::sensor_msgs::msg::PointCloud2 map;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(map);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(map);
      }

      bool operator==(const Response & other) const
      {
        return map == other.map;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace map_msgs

#endif  // MAP_MSGS_SRV_GETPOINTMAP_HPP_
