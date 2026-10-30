#ifndef LIFECYCLE_MSGS_SRV_GETAVAILABLETRANSITIONS_HPP_
#define LIFECYCLE_MSGS_SRV_GETAVAILABLETRANSITIONS_HPP_

#include "lifecycle_msgs/msg/transition_description.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace lifecycle_msgs
{
namespace srv
{

struct GetAvailableTransitions
{
  static constexpr const char * ros2_service_type = "lifecycle_msgs/srv/GetAvailableTransitions";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "lifecycle_msgs/srv/GetAvailableTransitions_Request";
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
      static constexpr const char * ros2_message_type = "lifecycle_msgs/srv/GetAvailableTransitions_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _available_transitions_type = std::vector<::lifecycle_msgs::msg::TransitionDescription>;
      std::vector<::lifecycle_msgs::msg::TransitionDescription> available_transitions;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(available_transitions);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(available_transitions);
      }

      bool operator==(const Response & other) const
      {
        return available_transitions == other.available_transitions;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace lifecycle_msgs

#endif  // LIFECYCLE_MSGS_SRV_GETAVAILABLETRANSITIONS_HPP_
