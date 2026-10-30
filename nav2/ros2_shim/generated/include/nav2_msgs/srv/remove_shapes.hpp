#ifndef NAV2_MSGS_SRV_REMOVESHAPES_HPP_
#define NAV2_MSGS_SRV_REMOVESHAPES_HPP_

#include "unique_identifier_msgs/msg/uuid.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace srv
{

struct RemoveShapes
{
  static constexpr const char * ros2_service_type = "nav2_msgs/srv/RemoveShapes";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/RemoveShapes_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _all_objects_type = bool;
      bool all_objects = false;
      using _uuids_type = std::vector<::unique_identifier_msgs::msg::UUID>;
      std::vector<::unique_identifier_msgs::msg::UUID> uuids;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(all_objects);
        ar(uuids);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(all_objects);
        ar(uuids);
      }

      bool operator==(const Request & other) const
      {
        return all_objects == other.all_objects &&
          uuids == other.uuids;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/RemoveShapes_Response";
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

#endif  // NAV2_MSGS_SRV_REMOVESHAPES_HPP_
