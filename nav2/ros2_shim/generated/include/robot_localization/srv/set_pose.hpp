#ifndef ROBOT_LOCALIZATION_SRV_SETPOSE_HPP_
#define ROBOT_LOCALIZATION_SRV_SETPOSE_HPP_

#include "geometry_msgs/msg/pose_with_covariance_stamped.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace robot_localization
{
namespace srv
{

struct SetPose
{
  static constexpr const char * ros2_service_type = "robot_localization/srv/SetPose";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "robot_localization/srv/SetPose_Request";
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
      static constexpr const char * ros2_message_type = "robot_localization/srv/SetPose_Response";
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
}  // namespace robot_localization

#endif  // ROBOT_LOCALIZATION_SRV_SETPOSE_HPP_
