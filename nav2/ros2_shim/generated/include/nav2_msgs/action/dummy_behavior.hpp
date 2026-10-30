#ifndef NAV2_MSGS_ACTION_DUMMYBEHAVIOR_HPP_
#define NAV2_MSGS_ACTION_DUMMYBEHAVIOR_HPP_

#include "builtin_interfaces/msg/duration.hpp"
#include "std_msgs/msg/string.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace action
{

struct DummyBehavior
{
    struct Goal
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/DummyBehavior_Goal";
      using SharedPtr = std::shared_ptr<Goal>;
      using ConstSharedPtr = std::shared_ptr<const Goal>;
      using UniquePtr = std::unique_ptr<Goal>;
      using _command_type = ::std_msgs::msg::String;
      ::std_msgs::msg::String command;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(command);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(command);
      }

      bool operator==(const Goal & other) const
      {
        return command == other.command;
      }
      bool operator!=(const Goal & other) const {return !(*this == other);}
    };

    struct Result
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/DummyBehavior_Result";
      using SharedPtr = std::shared_ptr<Result>;
      using ConstSharedPtr = std::shared_ptr<const Result>;
      using UniquePtr = std::unique_ptr<Result>;
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
      static constexpr const char * ros2_message_type = "nav2_msgs/action/DummyBehavior_Feedback";
      using SharedPtr = std::shared_ptr<Feedback>;
      using ConstSharedPtr = std::shared_ptr<const Feedback>;
      using UniquePtr = std::unique_ptr<Feedback>;

      template <class Archive>
      void visit(Archive & ar)
      {
        (void)ar;
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        (void)ar;
      }

      bool operator==(const Feedback & other) const
      {
        (void)other;
        return true;
      }
      bool operator!=(const Feedback & other) const {return !(*this == other);}
    };
};

}  // namespace action
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_ACTION_DUMMYBEHAVIOR_HPP_
