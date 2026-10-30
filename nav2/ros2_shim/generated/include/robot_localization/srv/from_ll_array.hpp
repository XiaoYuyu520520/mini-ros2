#ifndef ROBOT_LOCALIZATION_SRV_FROMLLARRAY_HPP_
#define ROBOT_LOCALIZATION_SRV_FROMLLARRAY_HPP_

#include "geographic_msgs/msg/geo_point.hpp"
#include "geometry_msgs/msg/point.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace robot_localization
{
namespace srv
{

struct FromLLArray
{
  static constexpr const char * ros2_service_type = "robot_localization/srv/FromLLArray";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "robot_localization/srv/FromLLArray_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _ll_points_type = std::vector<::geographic_msgs::msg::GeoPoint>;
      std::vector<::geographic_msgs::msg::GeoPoint> ll_points;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(ll_points);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(ll_points);
      }

      bool operator==(const Request & other) const
      {
        return ll_points == other.ll_points;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "robot_localization/srv/FromLLArray_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _map_points_type = std::vector<::geometry_msgs::msg::Point>;
      std::vector<::geometry_msgs::msg::Point> map_points;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(map_points);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(map_points);
      }

      bool operator==(const Response & other) const
      {
        return map_points == other.map_points;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace robot_localization

#endif  // ROBOT_LOCALIZATION_SRV_FROMLLARRAY_HPP_
