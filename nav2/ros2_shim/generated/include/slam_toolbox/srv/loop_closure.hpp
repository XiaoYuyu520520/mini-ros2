#ifndef SLAM_TOOLBOX_SRV_LOOPCLOSURE_HPP_
#define SLAM_TOOLBOX_SRV_LOOPCLOSURE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace slam_toolbox
{
namespace srv
{

struct LoopClosure
{
  static constexpr const char * ros2_service_type = "slam_toolbox/srv/LoopClosure";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "slam_toolbox/srv/LoopClosure_Request";
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
      static constexpr const char * ros2_message_type = "slam_toolbox/srv/LoopClosure_Response";
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
}  // namespace slam_toolbox

#endif  // SLAM_TOOLBOX_SRV_LOOPCLOSURE_HPP_
