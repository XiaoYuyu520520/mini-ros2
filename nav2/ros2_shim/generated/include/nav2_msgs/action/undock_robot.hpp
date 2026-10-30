#ifndef NAV2_MSGS_ACTION_UNDOCKROBOT_HPP_
#define NAV2_MSGS_ACTION_UNDOCKROBOT_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace action
{

struct UndockRobot
{
    struct Goal
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/UndockRobot_Goal";
      using SharedPtr = std::shared_ptr<Goal>;
      using ConstSharedPtr = std::shared_ptr<const Goal>;
      using UniquePtr = std::unique_ptr<Goal>;
      using _dock_type_type = std::string;
      std::string dock_type;
      using _max_undocking_time_type = float;
      float max_undocking_time = 0.0f;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(dock_type);
        ar(max_undocking_time);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(dock_type);
        ar(max_undocking_time);
      }

      bool operator==(const Goal & other) const
      {
        return dock_type == other.dock_type &&
          max_undocking_time == other.max_undocking_time;
      }
      bool operator!=(const Goal & other) const {return !(*this == other);}
    };

    struct Result
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/UndockRobot_Result";
      using SharedPtr = std::shared_ptr<Result>;
      using ConstSharedPtr = std::shared_ptr<const Result>;
      using UniquePtr = std::unique_ptr<Result>;
      static constexpr uint16_t NONE = 0;
      static constexpr uint16_t GOAL_REJECTED = 1;
      static constexpr uint16_t SEND_GOAL_FAILURE = 2;
      static constexpr uint16_t DOCK_NOT_VALID = 902;
      static constexpr uint16_t FAILED_TO_CONTROL = 905;
      static constexpr uint16_t TIMEOUT = 907;
      static constexpr uint16_t UNKNOWN = 999;

      using _success_type = bool;
      bool success = false;
      using _error_code_type = uint16_t;
      uint16_t error_code = 0;
      using _error_msg_type = std::string;
      std::string error_msg;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(success);
        ar(error_code);
        ar(error_msg);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(success);
        ar(error_code);
        ar(error_msg);
      }

      bool operator==(const Result & other) const
      {
        return success == other.success &&
          error_code == other.error_code &&
          error_msg == other.error_msg;
      }
      bool operator!=(const Result & other) const {return !(*this == other);}
    };

    struct Feedback
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/UndockRobot_Feedback";
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

#endif  // NAV2_MSGS_ACTION_UNDOCKROBOT_HPP_
