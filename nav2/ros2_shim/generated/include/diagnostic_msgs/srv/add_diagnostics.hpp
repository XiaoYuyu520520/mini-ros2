#ifndef DIAGNOSTIC_MSGS_SRV_ADDDIAGNOSTICS_HPP_
#define DIAGNOSTIC_MSGS_SRV_ADDDIAGNOSTICS_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace diagnostic_msgs
{
namespace srv
{

struct AddDiagnostics
{
  static constexpr const char * ros2_service_type = "diagnostic_msgs/srv/AddDiagnostics";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "diagnostic_msgs/srv/AddDiagnostics_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _load_namespace_type = std::string;
      std::string load_namespace;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(load_namespace);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(load_namespace);
      }

      bool operator==(const Request & other) const
      {
        return load_namespace == other.load_namespace;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "diagnostic_msgs/srv/AddDiagnostics_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _success_type = bool;
      bool success = false;
      using _message_type = std::string;
      std::string message;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(success);
        ar(message);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(success);
        ar(message);
      }

      bool operator==(const Response & other) const
      {
        return success == other.success &&
          message == other.message;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace diagnostic_msgs

#endif  // DIAGNOSTIC_MSGS_SRV_ADDDIAGNOSTICS_HPP_
