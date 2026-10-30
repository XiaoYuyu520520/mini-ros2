#ifndef MAP_MSGS_SRV_GETMAPROI_HPP_
#define MAP_MSGS_SRV_GETMAPROI_HPP_

#include "nav_msgs/msg/occupancy_grid.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace map_msgs
{
namespace srv
{

struct GetMapROI
{
  static constexpr const char * ros2_service_type = "map_msgs/srv/GetMapROI";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "map_msgs/srv/GetMapROI_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _x_type = double;
      double x = 0.0;
      using _y_type = double;
      double y = 0.0;
      using _l_x_type = double;
      double l_x = 0.0;
      using _l_y_type = double;
      double l_y = 0.0;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(x);
        ar(y);
        ar(l_x);
        ar(l_y);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(x);
        ar(y);
        ar(l_x);
        ar(l_y);
      }

      bool operator==(const Request & other) const
      {
        return x == other.x &&
          y == other.y &&
          l_x == other.l_x &&
          l_y == other.l_y;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "map_msgs/srv/GetMapROI_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _sub_map_type = ::nav_msgs::msg::OccupancyGrid;
      ::nav_msgs::msg::OccupancyGrid sub_map;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(sub_map);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(sub_map);
      }

      bool operator==(const Response & other) const
      {
        return sub_map == other.sub_map;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace map_msgs

#endif  // MAP_MSGS_SRV_GETMAPROI_HPP_
