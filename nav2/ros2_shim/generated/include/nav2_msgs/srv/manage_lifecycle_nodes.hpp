#ifndef NAV2_MSGS_SRV_MANAGELIFECYCLENODES_HPP_
#define NAV2_MSGS_SRV_MANAGELIFECYCLENODES_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace srv
{

struct ManageLifecycleNodes
{
  static constexpr const char * ros2_service_type = "nav2_msgs/srv/ManageLifecycleNodes";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/ManageLifecycleNodes_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      static constexpr uint8_t STARTUP = 0;
      static constexpr uint8_t PAUSE = 1;
      static constexpr uint8_t RESUME = 2;
      static constexpr uint8_t RESET = 3;
      static constexpr uint8_t SHUTDOWN = 4;
      static constexpr uint8_t CONFIGURE = 5;
      static constexpr uint8_t CLEANUP = 6;

      using _command_type = uint8_t;
      uint8_t command = 0;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(command);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(command);
      }

      bool operator==(const Request & other) const
      {
        return command == other.command;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/ManageLifecycleNodes_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _success_type = bool;
      bool success = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(success);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(success);
      }

      bool operator==(const Response & other) const
      {
        return success == other.success;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_SRV_MANAGELIFECYCLENODES_HPP_
