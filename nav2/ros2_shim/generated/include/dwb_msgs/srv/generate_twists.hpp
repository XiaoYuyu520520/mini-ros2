#ifndef DWB_MSGS_SRV_GENERATETWISTS_HPP_
#define DWB_MSGS_SRV_GENERATETWISTS_HPP_

#include "nav_2d_msgs/msg/twist2_d.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace dwb_msgs
{
namespace srv
{

struct GenerateTwists
{
  static constexpr const char * ros2_service_type = "dwb_msgs/srv/GenerateTwists";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "dwb_msgs/srv/GenerateTwists_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _current_vel_type = ::nav_2d_msgs::msg::Twist2D;
      ::nav_2d_msgs::msg::Twist2D current_vel;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(current_vel);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(current_vel);
      }

      bool operator==(const Request & other) const
      {
        return current_vel == other.current_vel;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "dwb_msgs/srv/GenerateTwists_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _twists_type = std::vector<::nav_2d_msgs::msg::Twist2D>;
      std::vector<::nav_2d_msgs::msg::Twist2D> twists;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(twists);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(twists);
      }

      bool operator==(const Response & other) const
      {
        return twists == other.twists;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace dwb_msgs

#endif  // DWB_MSGS_SRV_GENERATETWISTS_HPP_
