#ifndef NAV2_MSGS_SRV_LOADMAP_HPP_
#define NAV2_MSGS_SRV_LOADMAP_HPP_

#include "nav_msgs/msg/occupancy_grid.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace srv
{

struct LoadMap
{
  static constexpr const char * ros2_service_type = "nav2_msgs/srv/LoadMap";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/LoadMap_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _map_url_type = std::string;
      std::string map_url;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(map_url);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(map_url);
      }

      bool operator==(const Request & other) const
      {
        return map_url == other.map_url;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/LoadMap_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      static constexpr uint8_t RESULT_SUCCESS = 0;
      static constexpr uint8_t RESULT_MAP_DOES_NOT_EXIST = 1;
      static constexpr uint8_t RESULT_INVALID_MAP_DATA = 2;
      static constexpr uint8_t RESULT_INVALID_MAP_METADATA = 3;
      static constexpr uint8_t RESULT_UNDEFINED_FAILURE = 255;

      using _map_type = ::nav_msgs::msg::OccupancyGrid;
      ::nav_msgs::msg::OccupancyGrid map;
      using _result_type = uint8_t;
      uint8_t result = 0;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(map);
        ar(result);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(map);
        ar(result);
      }

      bool operator==(const Response & other) const
      {
        return map == other.map &&
          result == other.result;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_SRV_LOADMAP_HPP_
