#ifndef NAV2_MSGS_ACTION_SMOOTHPATH_HPP_
#define NAV2_MSGS_ACTION_SMOOTHPATH_HPP_

#include "builtin_interfaces/msg/duration.hpp"
#include "nav_msgs/msg/path.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace action
{

struct SmoothPath
{
    struct Goal
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/SmoothPath_Goal";
      using SharedPtr = std::shared_ptr<Goal>;
      using ConstSharedPtr = std::shared_ptr<const Goal>;
      using UniquePtr = std::unique_ptr<Goal>;
      using _path_type = ::nav_msgs::msg::Path;
      ::nav_msgs::msg::Path path;
      using _smoother_id_type = std::string;
      std::string smoother_id;
      using _max_smoothing_duration_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration max_smoothing_duration;
      using _check_for_collisions_type = bool;
      bool check_for_collisions = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(path);
        ar(smoother_id);
        ar(max_smoothing_duration);
        ar(check_for_collisions);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(path);
        ar(smoother_id);
        ar(max_smoothing_duration);
        ar(check_for_collisions);
      }

      bool operator==(const Goal & other) const
      {
        return path == other.path &&
          smoother_id == other.smoother_id &&
          max_smoothing_duration == other.max_smoothing_duration &&
          check_for_collisions == other.check_for_collisions;
      }
      bool operator!=(const Goal & other) const {return !(*this == other);}
    };

    struct Result
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/SmoothPath_Result";
      using SharedPtr = std::shared_ptr<Result>;
      using ConstSharedPtr = std::shared_ptr<const Result>;
      using UniquePtr = std::unique_ptr<Result>;
      static constexpr uint16_t NONE = 0;
      static constexpr uint16_t GOAL_REJECTED = 1;
      static constexpr uint16_t SEND_GOAL_FAILURE = 2;
      static constexpr uint16_t UNKNOWN = 500;
      static constexpr uint16_t INVALID_SMOOTHER = 501;
      static constexpr uint16_t TIMEOUT = 502;
      static constexpr uint16_t SMOOTHED_PATH_IN_COLLISION = 503;
      static constexpr uint16_t FAILED_TO_SMOOTH_PATH = 504;
      static constexpr uint16_t INVALID_PATH = 505;

      using _path_type = ::nav_msgs::msg::Path;
      ::nav_msgs::msg::Path path;
      using _smoothing_duration_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration smoothing_duration;
      using _was_completed_type = bool;
      bool was_completed = false;
      using _error_code_type = uint16_t;
      uint16_t error_code = 0;
      using _error_msg_type = std::string;
      std::string error_msg;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(path);
        ar(smoothing_duration);
        ar(was_completed);
        ar(error_code);
        ar(error_msg);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(path);
        ar(smoothing_duration);
        ar(was_completed);
        ar(error_code);
        ar(error_msg);
      }

      bool operator==(const Result & other) const
      {
        return path == other.path &&
          smoothing_duration == other.smoothing_duration &&
          was_completed == other.was_completed &&
          error_code == other.error_code &&
          error_msg == other.error_msg;
      }
      bool operator!=(const Result & other) const {return !(*this == other);}
    };

    struct Feedback
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/SmoothPath_Feedback";
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

#endif  // NAV2_MSGS_ACTION_SMOOTHPATH_HPP_
