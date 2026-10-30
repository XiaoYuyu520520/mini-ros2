#ifndef LIFECYCLE_MSGS_SRV_GETSTATE_HPP_
#define LIFECYCLE_MSGS_SRV_GETSTATE_HPP_

#include "lifecycle_msgs/msg/state.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace lifecycle_msgs
{
namespace srv
{

struct GetState
{
  static constexpr const char * ros2_service_type = "lifecycle_msgs/srv/GetState";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "lifecycle_msgs/srv/GetState_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;

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

      bool operator==(const Request & other) const
      {
        (void)other;
        return true;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "lifecycle_msgs/srv/GetState_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _current_state_type = ::lifecycle_msgs::msg::State;
      ::lifecycle_msgs::msg::State current_state;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(current_state);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(current_state);
      }

      bool operator==(const Response & other) const
      {
        return current_state == other.current_state;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace lifecycle_msgs

#endif  // LIFECYCLE_MSGS_SRV_GETSTATE_HPP_
