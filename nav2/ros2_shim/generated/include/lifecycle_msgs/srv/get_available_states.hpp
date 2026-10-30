#ifndef LIFECYCLE_MSGS_SRV_GETAVAILABLESTATES_HPP_
#define LIFECYCLE_MSGS_SRV_GETAVAILABLESTATES_HPP_

#include "lifecycle_msgs/msg/state.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace lifecycle_msgs
{
namespace srv
{

struct GetAvailableStates
{
  static constexpr const char * ros2_service_type = "lifecycle_msgs/srv/GetAvailableStates";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "lifecycle_msgs/srv/GetAvailableStates_Request";
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
      static constexpr const char * ros2_message_type = "lifecycle_msgs/srv/GetAvailableStates_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _available_states_type = std::vector<::lifecycle_msgs::msg::State>;
      std::vector<::lifecycle_msgs::msg::State> available_states;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(available_states);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(available_states);
      }

      bool operator==(const Response & other) const
      {
        return available_states == other.available_states;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace lifecycle_msgs

#endif  // LIFECYCLE_MSGS_SRV_GETAVAILABLESTATES_HPP_
