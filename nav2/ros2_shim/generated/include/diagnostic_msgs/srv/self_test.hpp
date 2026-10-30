#ifndef DIAGNOSTIC_MSGS_SRV_SELFTEST_HPP_
#define DIAGNOSTIC_MSGS_SRV_SELFTEST_HPP_

#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace diagnostic_msgs
{
namespace srv
{

struct SelfTest
{
  static constexpr const char * ros2_service_type = "diagnostic_msgs/srv/SelfTest";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "diagnostic_msgs/srv/SelfTest_Request";
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
      static constexpr const char * ros2_message_type = "diagnostic_msgs/srv/SelfTest_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _id_type = std::string;
      std::string id;
      using _passed_type = uint8_t;
      uint8_t passed = 0;
      using _status_type = std::vector<::diagnostic_msgs::msg::DiagnosticStatus>;
      std::vector<::diagnostic_msgs::msg::DiagnosticStatus> status;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(id);
        ar(passed);
        ar(status);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(id);
        ar(passed);
        ar(status);
      }

      bool operator==(const Response & other) const
      {
        return id == other.id &&
          passed == other.passed &&
          status == other.status;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace diagnostic_msgs

#endif  // DIAGNOSTIC_MSGS_SRV_SELFTEST_HPP_
