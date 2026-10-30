#ifndef NAV2_MSGS_ACTION_ASSISTEDTELEOP_HPP_
#define NAV2_MSGS_ACTION_ASSISTEDTELEOP_HPP_

#include "builtin_interfaces/msg/duration.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace action
{

struct AssistedTeleop
{
    struct Goal
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/AssistedTeleop_Goal";
      using SharedPtr = std::shared_ptr<Goal>;
      using ConstSharedPtr = std::shared_ptr<const Goal>;
      using UniquePtr = std::unique_ptr<Goal>;
      using _time_allowance_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration time_allowance;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(time_allowance);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(time_allowance);
      }

      bool operator==(const Goal & other) const
      {
        return time_allowance == other.time_allowance;
      }
      bool operator!=(const Goal & other) const {return !(*this == other);}
    };

    struct Result
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/AssistedTeleop_Result";
      using SharedPtr = std::shared_ptr<Result>;
      using ConstSharedPtr = std::shared_ptr<const Result>;
      using UniquePtr = std::unique_ptr<Result>;
      static constexpr uint16_t NONE = 0;
      static constexpr uint16_t GOAL_REJECTED = 1;
      static constexpr uint16_t SEND_GOAL_FAILURE = 2;
      static constexpr uint16_t UNKNOWN = 730;
      static constexpr uint16_t TIMEOUT = 731;
      static constexpr uint16_t TF_ERROR = 732;

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
      static constexpr const char * ros2_message_type = "nav2_msgs/action/AssistedTeleop_Feedback";
      using SharedPtr = std::shared_ptr<Feedback>;
      using ConstSharedPtr = std::shared_ptr<const Feedback>;
      using UniquePtr = std::unique_ptr<Feedback>;
      using _current_teleop_duration_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration current_teleop_duration;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(current_teleop_duration);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(current_teleop_duration);
      }

      bool operator==(const Feedback & other) const
      {
        return current_teleop_duration == other.current_teleop_duration;
      }
      bool operator!=(const Feedback & other) const {return !(*this == other);}
    };
};

}  // namespace action
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_ACTION_ASSISTEDTELEOP_HPP_
