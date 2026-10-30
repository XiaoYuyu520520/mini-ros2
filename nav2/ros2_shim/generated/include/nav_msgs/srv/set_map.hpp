#ifndef NAV_MSGS_SRV_SETMAP_HPP_
#define NAV_MSGS_SRV_SETMAP_HPP_

#include "geometry_msgs/msg/pose_with_covariance_stamped.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav_msgs
{
namespace srv
{

struct SetMap
{
  static constexpr const char * ros2_service_type = "nav_msgs/srv/SetMap";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav_msgs/srv/SetMap_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _map_type = ::nav_msgs::msg::OccupancyGrid;
      ::nav_msgs::msg::OccupancyGrid map;
      using _initial_pose_type = ::geometry_msgs::msg::PoseWithCovarianceStamped;
      ::geometry_msgs::msg::PoseWithCovarianceStamped initial_pose;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(map);
        ar(initial_pose);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(map);
        ar(initial_pose);
      }

      bool operator==(const Request & other) const
      {
        return map == other.map &&
          initial_pose == other.initial_pose;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav_msgs/srv/SetMap_Response";
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
}  // namespace nav_msgs

#endif  // NAV_MSGS_SRV_SETMAP_HPP_
