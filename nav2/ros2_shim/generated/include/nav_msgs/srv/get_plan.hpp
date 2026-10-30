#ifndef NAV_MSGS_SRV_GETPLAN_HPP_
#define NAV_MSGS_SRV_GETPLAN_HPP_

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "nav_msgs/msg/path.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav_msgs
{
namespace srv
{

struct GetPlan
{
  static constexpr const char * ros2_service_type = "nav_msgs/srv/GetPlan";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav_msgs/srv/GetPlan_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _start_type = ::geometry_msgs::msg::PoseStamped;
      ::geometry_msgs::msg::PoseStamped start;
      using _goal_type = ::geometry_msgs::msg::PoseStamped;
      ::geometry_msgs::msg::PoseStamped goal;
      using _tolerance_type = float;
      float tolerance = 0.0f;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(start);
        ar(goal);
        ar(tolerance);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(start);
        ar(goal);
        ar(tolerance);
      }

      bool operator==(const Request & other) const
      {
        return start == other.start &&
          goal == other.goal &&
          tolerance == other.tolerance;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav_msgs/srv/GetPlan_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _plan_type = ::nav_msgs::msg::Path;
      ::nav_msgs::msg::Path plan;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(plan);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(plan);
      }

      bool operator==(const Response & other) const
      {
        return plan == other.plan;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace nav_msgs

#endif  // NAV_MSGS_SRV_GETPLAN_HPP_
