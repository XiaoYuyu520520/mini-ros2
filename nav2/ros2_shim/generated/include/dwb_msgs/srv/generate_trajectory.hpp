#ifndef DWB_MSGS_SRV_GENERATETRAJECTORY_HPP_
#define DWB_MSGS_SRV_GENERATETRAJECTORY_HPP_

#include "dwb_msgs/msg/trajectory2_d.hpp"
#include "geometry_msgs/msg/pose.hpp"
#include "nav_2d_msgs/msg/twist2_d.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace dwb_msgs
{
namespace srv
{

struct GenerateTrajectory
{
  static constexpr const char * ros2_service_type = "dwb_msgs/srv/GenerateTrajectory";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "dwb_msgs/srv/GenerateTrajectory_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _start_pose_type = ::geometry_msgs::msg::Pose;
      ::geometry_msgs::msg::Pose start_pose;
      using _start_vel_type = ::nav_2d_msgs::msg::Twist2D;
      ::nav_2d_msgs::msg::Twist2D start_vel;
      using _cmd_vel_type = ::nav_2d_msgs::msg::Twist2D;
      ::nav_2d_msgs::msg::Twist2D cmd_vel;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(start_pose);
        ar(start_vel);
        ar(cmd_vel);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(start_pose);
        ar(start_vel);
        ar(cmd_vel);
      }

      bool operator==(const Request & other) const
      {
        return start_pose == other.start_pose &&
          start_vel == other.start_vel &&
          cmd_vel == other.cmd_vel;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "dwb_msgs/srv/GenerateTrajectory_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _traj_type = ::dwb_msgs::msg::Trajectory2D;
      ::dwb_msgs::msg::Trajectory2D traj;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(traj);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(traj);
      }

      bool operator==(const Response & other) const
      {
        return traj == other.traj;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace dwb_msgs

#endif  // DWB_MSGS_SRV_GENERATETRAJECTORY_HPP_
