#ifndef NAV2_MSGS_SRV_RELOADDOCKDATABASE_HPP_
#define NAV2_MSGS_SRV_RELOADDOCKDATABASE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace srv
{

struct ReloadDockDatabase
{
  static constexpr const char * ros2_service_type = "nav2_msgs/srv/ReloadDockDatabase";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/ReloadDockDatabase_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _filepath_type = std::string;
      std::string filepath;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(filepath);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(filepath);
      }

      bool operator==(const Request & other) const
      {
        return filepath == other.filepath;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/ReloadDockDatabase_Response";
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

#endif  // NAV2_MSGS_SRV_RELOADDOCKDATABASE_HPP_
