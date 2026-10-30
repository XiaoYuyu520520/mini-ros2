#ifndef SENSOR_MSGS_SRV_SETCAMERAINFO_HPP_
#define SENSOR_MSGS_SRV_SETCAMERAINFO_HPP_

#include "sensor_msgs/msg/camera_info.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace srv
{

struct SetCameraInfo
{
  static constexpr const char * ros2_service_type = "sensor_msgs/srv/SetCameraInfo";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "sensor_msgs/srv/SetCameraInfo_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _camera_info_type = ::sensor_msgs::msg::CameraInfo;
      ::sensor_msgs::msg::CameraInfo camera_info;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(camera_info);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(camera_info);
      }

      bool operator==(const Request & other) const
      {
        return camera_info == other.camera_info;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "sensor_msgs/srv/SetCameraInfo_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _success_type = bool;
      bool success = false;
      using _status_message_type = std::string;
      std::string status_message;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(success);
        ar(status_message);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(success);
        ar(status_message);
      }

      bool operator==(const Response & other) const
      {
        return success == other.success &&
          status_message == other.status_message;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_SRV_SETCAMERAINFO_HPP_
