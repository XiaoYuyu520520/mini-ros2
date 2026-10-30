#ifndef MAP_MSGS_SRV_SAVEMAP_HPP_
#define MAP_MSGS_SRV_SAVEMAP_HPP_

#include "std_msgs/msg/string.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace map_msgs
{
namespace srv
{

struct SaveMap
{
  static constexpr const char * ros2_service_type = "map_msgs/srv/SaveMap";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "map_msgs/srv/SaveMap_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _filename_type = ::std_msgs::msg::String;
      ::std_msgs::msg::String filename;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(filename);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(filename);
      }

      bool operator==(const Request & other) const
      {
        return filename == other.filename;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "map_msgs/srv/SaveMap_Response";
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
}  // namespace map_msgs

#endif  // MAP_MSGS_SRV_SAVEMAP_HPP_
