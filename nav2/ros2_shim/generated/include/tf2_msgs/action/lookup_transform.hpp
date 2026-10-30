#ifndef TF2_MSGS_ACTION_LOOKUPTRANSFORM_HPP_
#define TF2_MSGS_ACTION_LOOKUPTRANSFORM_HPP_

#include "builtin_interfaces/msg/duration.hpp"
#include "builtin_interfaces/msg/time.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "tf2_msgs/msg/tf2_error.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace tf2_msgs
{
namespace action
{

struct LookupTransform
{
    struct Goal
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "tf2_msgs/action/LookupTransform_Goal";
      using SharedPtr = std::shared_ptr<Goal>;
      using ConstSharedPtr = std::shared_ptr<const Goal>;
      using UniquePtr = std::unique_ptr<Goal>;
      using _target_frame_type = std::string;
      std::string target_frame;
      using _source_frame_type = std::string;
      std::string source_frame;
      using _source_time_type = ::builtin_interfaces::msg::Time;
      ::builtin_interfaces::msg::Time source_time;
      using _timeout_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration timeout;
      using _target_time_type = ::builtin_interfaces::msg::Time;
      ::builtin_interfaces::msg::Time target_time;
      using _fixed_frame_type = std::string;
      std::string fixed_frame;
      using _advanced_type = bool;
      bool advanced = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(target_frame);
        ar(source_frame);
        ar(source_time);
        ar(timeout);
        ar(target_time);
        ar(fixed_frame);
        ar(advanced);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(target_frame);
        ar(source_frame);
        ar(source_time);
        ar(timeout);
        ar(target_time);
        ar(fixed_frame);
        ar(advanced);
      }

      bool operator==(const Goal & other) const
      {
        return target_frame == other.target_frame &&
          source_frame == other.source_frame &&
          source_time == other.source_time &&
          timeout == other.timeout &&
          target_time == other.target_time &&
          fixed_frame == other.fixed_frame &&
          advanced == other.advanced;
      }
      bool operator!=(const Goal & other) const {return !(*this == other);}
    };

    struct Result
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "tf2_msgs/action/LookupTransform_Result";
      using SharedPtr = std::shared_ptr<Result>;
      using ConstSharedPtr = std::shared_ptr<const Result>;
      using UniquePtr = std::unique_ptr<Result>;
      using _transform_type = ::geometry_msgs::msg::TransformStamped;
      ::geometry_msgs::msg::TransformStamped transform;
      using _error_type = ::tf2_msgs::msg::TF2Error;
      ::tf2_msgs::msg::TF2Error error;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(transform);
        ar(error);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(transform);
        ar(error);
      }

      bool operator==(const Result & other) const
      {
        return transform == other.transform &&
          error == other.error;
      }
      bool operator!=(const Result & other) const {return !(*this == other);}
    };

    struct Feedback
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "tf2_msgs/action/LookupTransform_Feedback";
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
}  // namespace tf2_msgs

#endif  // TF2_MSGS_ACTION_LOOKUPTRANSFORM_HPP_
