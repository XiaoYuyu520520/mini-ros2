#ifndef NAV2_MSGS_SRV_SETINITIALPOSE_HPP_
#define NAV2_MSGS_SRV_SETINITIALPOSE_HPP_

#include "geometry_msgs/msg/pose_with_covariance_stamped.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace srv
{

struct SetInitialPose
{
  static constexpr const char * ros2_service_type = "nav2_msgs/srv/SetInitialPose";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/SetInitialPose_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _pose_type = ::geometry_msgs::msg::PoseWithCovarianceStamped;
      ::geometry_msgs::msg::PoseWithCovarianceStamped pose;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(pose);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(pose);
      }

      bool operator==(const Request & other) const
      {
        return pose == other.pose;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/SetInitialPose_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;

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

      bool operator==(const Response & other) const
      {
        (void)other;
        return true;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_SRV_SETINITIALPOSE_HPP_
