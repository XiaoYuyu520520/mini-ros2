#ifndef NAV2_MSGS_SRV_GETCOSTS_HPP_
#define NAV2_MSGS_SRV_GETCOSTS_HPP_

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace srv
{

struct GetCosts
{
  static constexpr const char * ros2_service_type = "nav2_msgs/srv/GetCosts";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/GetCosts_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _use_footprint_type = bool;
      bool use_footprint = false;
      using _poses_type = std::vector<::geometry_msgs::msg::PoseStamped>;
      std::vector<::geometry_msgs::msg::PoseStamped> poses;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(use_footprint);
        ar(poses);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(use_footprint);
        ar(poses);
      }

      bool operator==(const Request & other) const
      {
        return use_footprint == other.use_footprint &&
          poses == other.poses;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/GetCosts_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _costs_type = std::vector<float>;
      std::vector<float> costs;
      using _success_type = bool;
      bool success = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(costs);
        ar(success);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(costs);
        ar(success);
      }

      bool operator==(const Response & other) const
      {
        return costs == other.costs &&
          success == other.success;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_SRV_GETCOSTS_HPP_
