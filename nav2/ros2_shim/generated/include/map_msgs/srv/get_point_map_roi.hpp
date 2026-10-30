#ifndef MAP_MSGS_SRV_GETPOINTMAPROI_HPP_
#define MAP_MSGS_SRV_GETPOINTMAPROI_HPP_

#include "sensor_msgs/msg/point_cloud2.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace map_msgs
{
namespace srv
{

struct GetPointMapROI
{
  static constexpr const char * ros2_service_type = "map_msgs/srv/GetPointMapROI";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "map_msgs/srv/GetPointMapROI_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _x_type = double;
      double x = 0.0;
      using _y_type = double;
      double y = 0.0;
      using _z_type = double;
      double z = 0.0;
      using _r_type = double;
      double r = 0.0;
      using _l_x_type = double;
      double l_x = 0.0;
      using _l_y_type = double;
      double l_y = 0.0;
      using _l_z_type = double;
      double l_z = 0.0;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(x);
        ar(y);
        ar(z);
        ar(r);
        ar(l_x);
        ar(l_y);
        ar(l_z);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(x);
        ar(y);
        ar(z);
        ar(r);
        ar(l_x);
        ar(l_y);
        ar(l_z);
      }

      bool operator==(const Request & other) const
      {
        return x == other.x &&
          y == other.y &&
          z == other.z &&
          r == other.r &&
          l_x == other.l_x &&
          l_y == other.l_y &&
          l_z == other.l_z;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "map_msgs/srv/GetPointMapROI_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _sub_map_type = ::sensor_msgs::msg::PointCloud2;
      ::sensor_msgs::msg::PointCloud2 sub_map;

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

#endif  // MAP_MSGS_SRV_GETPOINTMAPROI_HPP_
