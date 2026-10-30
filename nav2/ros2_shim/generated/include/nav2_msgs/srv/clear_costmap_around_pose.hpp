#ifndef NAV2_MSGS_SRV_CLEARCOSTMAPAROUNDPOSE_HPP_
#define NAV2_MSGS_SRV_CLEARCOSTMAPAROUNDPOSE_HPP_

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace srv
{

struct ClearCostmapAroundPose
{
  static constexpr const char * ros2_service_type = "nav2_msgs/srv/ClearCostmapAroundPose";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/ClearCostmapAroundPose_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _pose_type = ::geometry_msgs::msg::PoseStamped;
      ::geometry_msgs::msg::PoseStamped pose;
      using _reset_distance_type = double;
      double reset_distance = 0.0;
      using _plugins_type = std::vector<std::string>;
      std::vector<std::string> plugins;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(pose);
        ar(reset_distance);
        ar(plugins);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(pose);
        ar(reset_distance);
        ar(plugins);
      }

      bool operator==(const Request & other) const
      {
        return pose == other.pose &&
          reset_distance == other.reset_distance &&
          plugins == other.plugins;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/ClearCostmapAroundPose_Response";
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

#endif  // NAV2_MSGS_SRV_CLEARCOSTMAPAROUNDPOSE_HPP_
