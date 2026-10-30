#ifndef DWB_MSGS_SRV_GETCRITICSCORE_HPP_
#define DWB_MSGS_SRV_GETCRITICSCORE_HPP_

#include "dwb_msgs/msg/critic_score.hpp"
#include "dwb_msgs/msg/trajectory2_d.hpp"
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

struct GetCriticScore
{
  static constexpr const char * ros2_service_type = "dwb_msgs/srv/GetCriticScore";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "dwb_msgs/srv/GetCriticScore_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _pose_type = ::geometry_msgs::msg::PoseStamped;
      ::geometry_msgs::msg::PoseStamped pose;
      using _velocity_type = ::nav_2d_msgs::msg::Twist2D;
      ::nav_2d_msgs::msg::Twist2D velocity;
      using _global_plan_type = ::nav_msgs::msg::Path;
      ::nav_msgs::msg::Path global_plan;
      using _traj_type = ::dwb_msgs::msg::Trajectory2D;
      ::dwb_msgs::msg::Trajectory2D traj;
      using _critic_name_type = std::string;
      std::string critic_name;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(pose);
        ar(velocity);
        ar(global_plan);
        ar(traj);
        ar(critic_name);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(pose);
        ar(velocity);
        ar(global_plan);
        ar(traj);
        ar(critic_name);
      }

      bool operator==(const Request & other) const
      {
        return pose == other.pose &&
          velocity == other.velocity &&
          global_plan == other.global_plan &&
          traj == other.traj &&
          critic_name == other.critic_name;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "dwb_msgs/srv/GetCriticScore_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _score_type = ::dwb_msgs::msg::CriticScore;
      ::dwb_msgs::msg::CriticScore score;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(score);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(score);
      }

      bool operator==(const Response & other) const
      {
        return score == other.score;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace dwb_msgs

#endif  // DWB_MSGS_SRV_GETCRITICSCORE_HPP_
