#ifndef NAV2_MSGS_SRV_ADDSHAPES_HPP_
#define NAV2_MSGS_SRV_ADDSHAPES_HPP_

#include "nav2_msgs/msg/circle_object.hpp"
#include "nav2_msgs/msg/polygon_object.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace srv
{

struct AddShapes
{
  static constexpr const char * ros2_service_type = "nav2_msgs/srv/AddShapes";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/AddShapes_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _circles_type = std::vector<::nav2_msgs::msg::CircleObject>;
      std::vector<::nav2_msgs::msg::CircleObject> circles;
      using _polygons_type = std::vector<::nav2_msgs::msg::PolygonObject>;
      std::vector<::nav2_msgs::msg::PolygonObject> polygons;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(circles);
        ar(polygons);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(circles);
        ar(polygons);
      }

      bool operator==(const Request & other) const
      {
        return circles == other.circles &&
          polygons == other.polygons;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/AddShapes_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _success_type = bool;
      bool success = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(success);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(success);
      }

      bool operator==(const Response & other) const
      {
        return success == other.success;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_SRV_ADDSHAPES_HPP_
