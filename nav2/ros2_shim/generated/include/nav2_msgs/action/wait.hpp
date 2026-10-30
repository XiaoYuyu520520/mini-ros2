#ifndef NAV2_MSGS_ACTION_WAIT_HPP_
#define NAV2_MSGS_ACTION_WAIT_HPP_

#include "builtin_interfaces/msg/duration.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace action
{

struct Wait
{
    struct Goal
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/Wait_Goal";
      using SharedPtr = std::shared_ptr<Goal>;
      using ConstSharedPtr = std::shared_ptr<const Goal>;
      using UniquePtr = std::unique_ptr<Goal>;
      using _time_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration time;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(time);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(time);
      }

      bool operator==(const Goal & other) const
      {
        return time == other.time;
      }
      bool operator!=(const Goal & other) const {return !(*this == other);}
    };

    struct Result
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/Wait_Result";
      using SharedPtr = std::shared_ptr<Result>;
      using ConstSharedPtr = std::shared_ptr<const Result>;
      using UniquePtr = std::unique_ptr<Result>;
      static constexpr uint16_t NONE = 0;
      static constexpr uint16_t GOAL_REJECTED = 1;
      static constexpr uint16_t SEND_GOAL_FAILURE = 2;
      static constexpr uint16_t UNKNOWN = 740;
      static constexpr uint16_t TIMEOUT = 741;

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
      static constexpr const char * ros2_message_type = "nav2_msgs/action/Wait_Feedback";
      using SharedPtr = std::shared_ptr<Feedback>;
      using ConstSharedPtr = std::shared_ptr<const Feedback>;
      using UniquePtr = std::unique_ptr<Feedback>;
      using _time_left_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration time_left;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(time_left);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(time_left);
      }

      bool operator==(const Feedback & other) const
      {
        return time_left == other.time_left;
      }
      bool operator!=(const Feedback & other) const {return !(*this == other);}
    };
};

}  // namespace action
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_ACTION_WAIT_HPP_
