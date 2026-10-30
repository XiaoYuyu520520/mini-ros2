#ifndef NAV2_MSGS_ACTION_FOLLOWOBJECT_HPP_
#define NAV2_MSGS_ACTION_FOLLOWOBJECT_HPP_

#include "builtin_interfaces/msg/duration.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace action
{

struct FollowObject
{
    struct Goal
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/FollowObject_Goal";
      using SharedPtr = std::shared_ptr<Goal>;
      using ConstSharedPtr = std::shared_ptr<const Goal>;
      using UniquePtr = std::unique_ptr<Goal>;
      using _pose_topic_type = std::string;
      std::string pose_topic;
      using _tracked_frame_type = std::string;
      std::string tracked_frame;
      using _max_duration_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration max_duration;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(pose_topic);
        ar(tracked_frame);
        ar(max_duration);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(pose_topic);
        ar(tracked_frame);
        ar(max_duration);
      }

      bool operator==(const Goal & other) const
      {
        return pose_topic == other.pose_topic &&
          tracked_frame == other.tracked_frame &&
          max_duration == other.max_duration;
      }
      bool operator!=(const Goal & other) const {return !(*this == other);}
    };

    struct Result
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/FollowObject_Result";
      using SharedPtr = std::shared_ptr<Result>;
      using ConstSharedPtr = std::shared_ptr<const Result>;
      using UniquePtr = std::unique_ptr<Result>;
      static constexpr uint16_t NONE = 0;
      static constexpr uint16_t GOAL_REJECTED = 1;
      static constexpr uint16_t SEND_GOAL_FAILURE = 2;
      static constexpr uint16_t TF_ERROR = 901;
      static constexpr uint16_t FAILED_TO_DETECT_OBJECT = 902;
      static constexpr uint16_t FAILED_TO_CONTROL = 903;
      static constexpr uint16_t TIMEOUT = 904;
      static constexpr uint16_t UNKNOWN = 999;

      using _total_elapsed_time_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration total_elapsed_time;
      using _error_code_type = uint16_t;
      uint16_t error_code = 0;
      using _num_retries_type = uint16_t;
      uint16_t num_retries = 0;
      using _error_msg_type = std::string;
      std::string error_msg;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(total_elapsed_time);
        ar(error_code);
        ar(num_retries);
        ar(error_msg);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(total_elapsed_time);
        ar(error_code);
        ar(num_retries);
        ar(error_msg);
      }

      bool operator==(const Result & other) const
      {
        return total_elapsed_time == other.total_elapsed_time &&
          error_code == other.error_code &&
          num_retries == other.num_retries &&
          error_msg == other.error_msg;
      }
      bool operator!=(const Result & other) const {return !(*this == other);}
    };

    struct Feedback
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/FollowObject_Feedback";
      using SharedPtr = std::shared_ptr<Feedback>;
      using ConstSharedPtr = std::shared_ptr<const Feedback>;
      using UniquePtr = std::unique_ptr<Feedback>;
      static constexpr uint16_t NONE = 0;
      static constexpr uint16_t INITIAL_PERCEPTION = 1;
      static constexpr uint16_t CONTROLLING = 2;
      static constexpr uint16_t STOPPING = 3;
      static constexpr uint16_t RETRY = 4;

      using _state_type = uint16_t;
      uint16_t state = 0;
      using _following_time_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration following_time;
      using _num_retries_type = uint16_t;
      uint16_t num_retries = 0;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(state);
        ar(following_time);
        ar(num_retries);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(state);
        ar(following_time);
        ar(num_retries);
      }

      bool operator==(const Feedback & other) const
      {
        return state == other.state &&
          following_time == other.following_time &&
          num_retries == other.num_retries;
      }
      bool operator!=(const Feedback & other) const {return !(*this == other);}
    };
};

}  // namespace action
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_ACTION_FOLLOWOBJECT_HPP_
