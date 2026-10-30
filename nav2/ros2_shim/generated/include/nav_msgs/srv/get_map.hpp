#ifndef NAV_MSGS_SRV_GETMAP_HPP_
#define NAV_MSGS_SRV_GETMAP_HPP_

#include "nav_msgs/msg/occupancy_grid.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav_msgs
{
namespace srv
{

struct GetMap
{
  static constexpr const char * ros2_service_type = "nav_msgs/srv/GetMap";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav_msgs/srv/GetMap_Request";
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
      static constexpr const char * ros2_message_type = "nav_msgs/srv/GetMap_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _map_type = ::nav_msgs::msg::OccupancyGrid;
      ::nav_msgs::msg::OccupancyGrid map;

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
}  // namespace nav_msgs

#endif  // NAV_MSGS_SRV_GETMAP_HPP_
