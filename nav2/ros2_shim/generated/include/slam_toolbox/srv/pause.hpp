#ifndef SLAM_TOOLBOX_SRV_PAUSE_HPP_
#define SLAM_TOOLBOX_SRV_PAUSE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace slam_toolbox
{
namespace srv
{

struct Pause
{
  static constexpr const char * ros2_service_type = "slam_toolbox/srv/Pause";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "slam_toolbox/srv/Pause_Request";
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
      static constexpr const char * ros2_message_type = "slam_toolbox/srv/Pause_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _status_type = bool;
      bool status = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(status);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(status);
      }

      bool operator==(const Response & other) const
      {
        return status == other.status;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace slam_toolbox

#endif  // SLAM_TOOLBOX_SRV_PAUSE_HPP_
