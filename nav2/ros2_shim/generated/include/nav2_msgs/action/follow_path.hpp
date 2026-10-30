#ifndef NAV2_MSGS_ACTION_FOLLOWPATH_HPP_
#define NAV2_MSGS_ACTION_FOLLOWPATH_HPP_

#include "nav2_msgs/msg/tracking_feedback.hpp"
#include "nav_msgs/msg/path.hpp"
#include "std_msgs/msg/empty.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace action
{

struct FollowPath
{
    struct Goal
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/FollowPath_Goal";
      using SharedPtr = std::shared_ptr<Goal>;
      using ConstSharedPtr = std::shared_ptr<const Goal>;
      using UniquePtr = std::unique_ptr<Goal>;
      using _path_type = ::nav_msgs::msg::Path;
      ::nav_msgs::msg::Path path;
      using _controller_id_type = std::string;
      std::string controller_id;
      using _goal_checker_id_type = std::string;
      std::string goal_checker_id;
      using _progress_checker_id_type = std::string;
      std::string progress_checker_id;
      using _path_handler_id_type = std::string;
      std::string path_handler_id;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(path);
        ar(controller_id);
        ar(goal_checker_id);
        ar(progress_checker_id);
        ar(path_handler_id);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(path);
        ar(controller_id);
        ar(goal_checker_id);
        ar(progress_checker_id);
        ar(path_handler_id);
      }

      bool operator==(const Goal & other) const
      {
        return path == other.path &&
          controller_id == other.controller_id &&
          goal_checker_id == other.goal_checker_id &&
          progress_checker_id == other.progress_checker_id &&
          path_handler_id == other.path_handler_id;
      }
      bool operator!=(const Goal & other) const {return !(*this == other);}
    };

    struct Result
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/FollowPath_Result";
      using SharedPtr = std::shared_ptr<Result>;
      using ConstSharedPtr = std::shared_ptr<const Result>;
      using UniquePtr = std::unique_ptr<Result>;
      static constexpr uint16_t NONE = 0;
      static constexpr uint16_t GOAL_REJECTED = 1;
      static constexpr uint16_t SEND_GOAL_FAILURE = 2;
      static constexpr uint16_t UNKNOWN = 100;
      static constexpr uint16_t INVALID_CONTROLLER = 101;
      static constexpr uint16_t TF_ERROR = 102;
      static constexpr uint16_t INVALID_PATH = 103;
      static constexpr uint16_t PATIENCE_EXCEEDED = 104;
      static constexpr uint16_t FAILED_TO_MAKE_PROGRESS = 105;
      static constexpr uint16_t NO_VALID_CONTROL = 106;
      static constexpr uint16_t CONTROLLER_TIMED_OUT = 107;
      static constexpr uint16_t TIMEOUT = 108;

      using _result_type = ::std_msgs::msg::Empty;
      ::std_msgs::msg::Empty result;
      using _error_code_type = uint16_t;
      uint16_t error_code = 0;
      using _error_msg_type = std::string;
      std::string error_msg;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(result);
        ar(error_code);
        ar(error_msg);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(result);
        ar(error_code);
        ar(error_msg);
      }

      bool operator==(const Result & other) const
      {
        return result == other.result &&
          error_code == other.error_code &&
          error_msg == other.error_msg;
      }
      bool operator!=(const Result & other) const {return !(*this == other);}
    };

    struct Feedback
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/FollowPath_Feedback";
      using SharedPtr = std::shared_ptr<Feedback>;
      using ConstSharedPtr = std::shared_ptr<const Feedback>;
      using UniquePtr = std::unique_ptr<Feedback>;
      using _tracking_feedback_type = ::nav2_msgs::msg::TrackingFeedback;
      ::nav2_msgs::msg::TrackingFeedback tracking_feedback;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(tracking_feedback);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(tracking_feedback);
      }

      bool operator==(const Feedback & other) const
      {
        return tracking_feedback == other.tracking_feedback;
      }
      bool operator!=(const Feedback & other) const {return !(*this == other);}
    };
};

}  // namespace action
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_ACTION_FOLLOWPATH_HPP_
