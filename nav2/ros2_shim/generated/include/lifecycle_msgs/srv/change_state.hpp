#ifndef LIFECYCLE_MSGS_SRV_CHANGESTATE_HPP_
#define LIFECYCLE_MSGS_SRV_CHANGESTATE_HPP_

#include "lifecycle_msgs/msg/transition.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace lifecycle_msgs
{
namespace srv
{

struct ChangeState
{
  static constexpr const char * ros2_service_type = "lifecycle_msgs/srv/ChangeState";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "lifecycle_msgs/srv/ChangeState_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _transition_type = ::lifecycle_msgs::msg::Transition;
      ::lifecycle_msgs::msg::Transition transition;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(transition);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(transition);
      }

      bool operator==(const Request & other) const
      {
        return transition == other.transition;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "lifecycle_msgs/srv/ChangeState_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _success_type = bool;
      bool success = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(success);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(success);
      }

      bool operator==(const Response & other) const
      {
        return success == other.success;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace lifecycle_msgs

#endif  // LIFECYCLE_MSGS_SRV_CHANGESTATE_HPP_
