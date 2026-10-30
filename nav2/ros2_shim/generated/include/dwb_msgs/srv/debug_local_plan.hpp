#ifndef DWB_MSGS_SRV_DEBUGLOCALPLAN_HPP_
#define DWB_MSGS_SRV_DEBUGLOCALPLAN_HPP_

#include "dwb_msgs/msg/local_plan_evaluation.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "nav_2d_msgs/msg/twist2_d.hpp"
#include "nav_msgs/msg/path.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace dwb_msgs
{
namespace srv
{

struct DebugLocalPlan
{
  static constexpr const char * ros2_service_type = "dwb_msgs/srv/DebugLocalPlan";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "dwb_msgs/srv/DebugLocalPlan_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _pose_type = ::geometry_msgs::msg::PoseStamped;
      ::geometry_msgs::msg::PoseStamped pose;
      using _velocity_type = ::nav_2d_msgs::msg::Twist2D;
      ::nav_2d_msgs::msg::Twist2D velocity;
      using _global_plan_type = ::nav_msgs::msg::Path;
      ::nav_msgs::msg::Path global_plan;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(pose);
        ar(velocity);
        ar(global_plan);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(pose);
        ar(velocity);
        ar(global_plan);
      }

      bool operator==(const Request & other) const
      {
        return pose == other.pose &&
          velocity == other.velocity &&
          global_plan == other.global_plan;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "dwb_msgs/srv/DebugLocalPlan_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _results_type = ::dwb_msgs::msg::LocalPlanEvaluation;
      ::dwb_msgs::msg::LocalPlanEvaluation results;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(results);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(results);
      }

      bool operator==(const Response & other) const
      {
        return results == other.results;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace dwb_msgs

#endif  // DWB_MSGS_SRV_DEBUGLOCALPLAN_HPP_
