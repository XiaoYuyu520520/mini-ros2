#ifndef ROBOT_LOCALIZATION_SRV_SETDATUM_HPP_
#define ROBOT_LOCALIZATION_SRV_SETDATUM_HPP_

#include "geographic_msgs/msg/geo_pose.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace robot_localization
{
namespace srv
{

struct SetDatum
{
  static constexpr const char * ros2_service_type = "robot_localization/srv/SetDatum";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "robot_localization/srv/SetDatum_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _geo_pose_type = ::geographic_msgs::msg::GeoPose;
      ::geographic_msgs::msg::GeoPose geo_pose;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(geo_pose);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(geo_pose);
      }

      bool operator==(const Request & other) const
      {
        return geo_pose == other.geo_pose;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "robot_localization/srv/SetDatum_Response";
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

#endif  // ROBOT_LOCALIZATION_SRV_SETDATUM_HPP_
