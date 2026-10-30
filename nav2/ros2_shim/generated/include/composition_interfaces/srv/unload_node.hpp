#ifndef COMPOSITION_INTERFACES_SRV_UNLOADNODE_HPP_
#define COMPOSITION_INTERFACES_SRV_UNLOADNODE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace composition_interfaces
{
namespace srv
{

struct UnloadNode
{
  static constexpr const char * ros2_service_type = "composition_interfaces/srv/UnloadNode";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "composition_interfaces/srv/UnloadNode_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _unique_id_type = uint64_t;
      uint64_t unique_id = 0;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(unique_id);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(unique_id);
      }

      bool operator==(const Request & other) const
      {
        return unique_id == other.unique_id;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "composition_interfaces/srv/UnloadNode_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _success_type = bool;
      bool success = false;
      using _error_message_type = std::string;
      std::string error_message;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(success);
        ar(error_message);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(success);
        ar(error_message);
      }

      bool operator==(const Response & other) const
      {
        return success == other.success &&
          error_message == other.error_message;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace composition_interfaces

#endif  // COMPOSITION_INTERFACES_SRV_UNLOADNODE_HPP_
