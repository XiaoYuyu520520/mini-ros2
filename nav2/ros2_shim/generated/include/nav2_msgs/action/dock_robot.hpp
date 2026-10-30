#ifndef NAV2_MSGS_ACTION_DOCKROBOT_HPP_
#define NAV2_MSGS_ACTION_DOCKROBOT_HPP_

#include "builtin_interfaces/msg/duration.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace action
{

struct DockRobot
{
    struct Goal
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/DockRobot_Goal";
      using SharedPtr = std::shared_ptr<Goal>;
      using ConstSharedPtr = std::shared_ptr<const Goal>;
      using UniquePtr = std::unique_ptr<Goal>;
      using _use_dock_id_type = bool;
      bool use_dock_id = false;
      using _dock_id_type = std::string;
      std::string dock_id;
      using _dock_pose_type = ::geometry_msgs::msg::PoseStamped;
      ::geometry_msgs::msg::PoseStamped dock_pose;
      using _dock_type_type = std::string;
      std::string dock_type;
      using _max_staging_time_type = float;
      float max_staging_time = 0.0f;
      using _navigate_to_staging_pose_type = bool;
      bool navigate_to_staging_pose = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(use_dock_id);
        ar(dock_id);
        ar(dock_pose);
        ar(dock_type);
        ar(max_staging_time);
        ar(navigate_to_staging_pose);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(use_dock_id);
        ar(dock_id);
        ar(dock_pose);
        ar(dock_type);
        ar(max_staging_time);
        ar(navigate_to_staging_pose);
      }

      bool operator==(const Goal & other) const
      {
        return use_dock_id == other.use_dock_id &&
          dock_id == other.dock_id &&
          dock_pose == other.dock_pose &&
          dock_type == other.dock_type &&
          max_staging_time == other.max_staging_time &&
          navigate_to_staging_pose == other.navigate_to_staging_pose;
      }
      bool operator!=(const Goal & other) const {return !(*this == other);}
    };

    struct Result
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/DockRobot_Result";
      using SharedPtr = std::shared_ptr<Result>;
      using ConstSharedPtr = std::shared_ptr<const Result>;
      using UniquePtr = std::unique_ptr<Result>;
      static constexpr uint16_t NONE = 0;
      static constexpr uint16_t GOAL_REJECTED = 1;
      static constexpr uint16_t SEND_GOAL_FAILURE = 2;
      static constexpr uint16_t DOCK_NOT_IN_DB = 901;
      static constexpr uint16_t DOCK_NOT_VALID = 902;
      static constexpr uint16_t FAILED_TO_STAGE = 903;
      static constexpr uint16_t FAILED_TO_DETECT_DOCK = 904;
      static constexpr uint16_t FAILED_TO_CONTROL = 905;
      static constexpr uint16_t FAILED_TO_CHARGE = 906;
      static constexpr uint16_t TIMEOUT = 907;
      static constexpr uint16_t UNKNOWN = 999;

      using _success_type = bool;
      bool success = false;
      using _error_code_type = uint16_t;
      uint16_t error_code = 0;
      using _num_retries_type = uint16_t;
      uint16_t num_retries = 0;
      using _error_msg_type = std::string;
      std::string error_msg;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(success);
        ar(error_code);
        ar(num_retries);
        ar(error_msg);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(success);
        ar(error_code);
        ar(num_retries);
        ar(error_msg);
      }

      bool operator==(const Result & other) const
      {
        return success == other.success &&
          error_code == other.error_code &&
          num_retries == other.num_retries &&
          error_msg == other.error_msg;
      }
      bool operator!=(const Result & other) const {return !(*this == other);}
    };

    struct Feedback
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/DockRobot_Feedback";
      using SharedPtr = std::shared_ptr<Feedback>;
      using ConstSharedPtr = std::shared_ptr<const Feedback>;
      using UniquePtr = std::unique_ptr<Feedback>;
      static constexpr uint16_t NONE = 0;
      static constexpr uint16_t NAV_TO_STAGING_POSE = 1;
      static constexpr uint16_t INITIAL_PERCEPTION = 2;
      static constexpr uint16_t CONTROLLING = 3;
      static constexpr uint16_t WAIT_FOR_CHARGE = 4;
      static constexpr uint16_t RETRY = 5;

      using _state_type = uint16_t;
      uint16_t state = 0;
      using _docking_time_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration docking_time;
      using _num_retries_type = uint16_t;
      uint16_t num_retries = 0;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(state);
        ar(docking_time);
        ar(num_retries);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(state);
        ar(docking_time);
        ar(num_retries);
      }

      bool operator==(const Feedback & other) const
      {
        return state == other.state &&
          docking_time == other.docking_time &&
          num_retries == other.num_retries;
      }
      bool operator!=(const Feedback & other) const {return !(*this == other);}
    };
};

}  // namespace action
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_ACTION_DOCKROBOT_HPP_
