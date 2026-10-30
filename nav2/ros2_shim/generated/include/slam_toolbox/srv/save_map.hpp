#ifndef SLAM_TOOLBOX_SRV_SAVEMAP_HPP_
#define SLAM_TOOLBOX_SRV_SAVEMAP_HPP_

#include "std_msgs/msg/string.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace slam_toolbox
{
namespace srv
{

struct SaveMap
{
  static constexpr const char * ros2_service_type = "slam_toolbox/srv/SaveMap";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "slam_toolbox/srv/SaveMap_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _name_type = ::std_msgs::msg::String;
      ::std_msgs::msg::String name;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(name);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(name);
      }

      bool operator==(const Request & other) const
      {
        return name == other.name;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "slam_toolbox/srv/SaveMap_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      static constexpr uint8_t RESULT_SUCCESS = 0;
      static constexpr uint8_t RESULT_NO_MAP_RECEIEVD = 1;
      static constexpr uint8_t RESULT_UNDEFINED_FAILURE = 255;

      using _result_type = uint8_t;
      uint8_t result = 0;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(result);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(result);
      }

      bool operator==(const Response & other) const
      {
        return result == other.result;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace slam_toolbox

#endif  // SLAM_TOOLBOX_SRV_SAVEMAP_HPP_
