#ifndef NAV2_MSGS_ACTION_SPIN_HPP_
#define NAV2_MSGS_ACTION_SPIN_HPP_

#include "builtin_interfaces/msg/duration.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace action
{

struct Spin
{
    struct Goal
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/Spin_Goal";
      using SharedPtr = std::shared_ptr<Goal>;
      using ConstSharedPtr = std::shared_ptr<const Goal>;
      using UniquePtr = std::unique_ptr<Goal>;
      using _target_yaw_type = float;
      float target_yaw = 0.0f;
      using _time_allowance_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration time_allowance;
      using _disable_collision_checks_type = bool;
      bool disable_collision_checks = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(target_yaw);
        ar(time_allowance);
        ar(disable_collision_checks);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(target_yaw);
        ar(time_allowance);
        ar(disable_collision_checks);
      }

      bool operator==(const Goal & other) const
      {
        return target_yaw == other.target_yaw &&
          time_allowance == other.time_allowance &&
          disable_collision_checks == other.disable_collision_checks;
      }
      bool operator!=(const Goal & other) const {return !(*this == other);}
    };

    struct Result
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/Spin_Result";
      using SharedPtr = std::shared_ptr<Result>;
      using ConstSharedPtr = std::shared_ptr<const Result>;
      using UniquePtr = std::unique_ptr<Result>;
      static constexpr uint16_t NONE = 0;
      static constexpr uint16_t GOAL_REJECTED = 1;
      static constexpr uint16_t SEND_GOAL_FAILURE = 2;
      static constexpr uint16_t UNKNOWN = 700;
      static constexpr uint16_t TIMEOUT = 701;
      static constexpr uint16_t TF_ERROR = 702;
      static constexpr uint16_t COLLISION_AHEAD = 703;

      using _total_elapsed_time_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration total_elapsed_time;
      using _error_code_type = uint16_t;
      uint16_t error_code = 0;
      using _error_msg_type = std::string;
      std::string error_msg;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(total_elapsed_time);
        ar(error_code);
        ar(error_msg);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(total_elapsed_time);
        ar(error_code);
        ar(error_msg);
      }

      bool operator==(const Result & other) const
      {
        return total_elapsed_time == other.total_elapsed_time &&
          error_code == other.error_code &&
          error_msg == other.error_msg;
      }
      bool operator!=(const Result & other) const {return !(*this == other);}
    };

    struct Feedback
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/Spin_Feedback";
      using SharedPtr = std::shared_ptr<Feedback>;
      using ConstSharedPtr = std::shared_ptr<const Feedback>;
      using UniquePtr = std::unique_ptr<Feedback>;
      using _angular_distance_traveled_type = float;
      float angular_distance_traveled = 0.0f;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(angular_distance_traveled);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(angular_distance_traveled);
      }

      bool operator==(const Feedback & other) const
      {
        return angular_distance_traveled == other.angular_distance_traveled;
      }
      bool operator!=(const Feedback & other) const {return !(*this == other);}
    };
};

}  // namespace action
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_ACTION_SPIN_HPP_
