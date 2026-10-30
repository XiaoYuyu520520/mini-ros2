#ifndef ROBOT_LOCALIZATION_SRV_TOLL_HPP_
#define ROBOT_LOCALIZATION_SRV_TOLL_HPP_

#include "geographic_msgs/msg/geo_point.hpp"
#include "geometry_msgs/msg/point.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace robot_localization
{
namespace srv
{

struct ToLL
{
  static constexpr const char * ros2_service_type = "robot_localization/srv/ToLL";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "robot_localization/srv/ToLL_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _map_point_type = ::geometry_msgs::msg::Point;
      ::geometry_msgs::msg::Point map_point;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(map_point);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(map_point);
      }

      bool operator==(const Request & other) const
      {
        return map_point == other.map_point;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "robot_localization/srv/ToLL_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _ll_point_type = ::geographic_msgs::msg::GeoPoint;
      ::geographic_msgs::msg::GeoPoint ll_point;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(ll_point);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(ll_point);
      }

      bool operator==(const Response & other) const
      {
        return ll_point == other.ll_point;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace robot_localization

#endif  // ROBOT_LOCALIZATION_SRV_TOLL_HPP_
