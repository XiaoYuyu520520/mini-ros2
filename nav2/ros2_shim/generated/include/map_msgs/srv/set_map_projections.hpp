#ifndef MAP_MSGS_SRV_SETMAPPROJECTIONS_HPP_
#define MAP_MSGS_SRV_SETMAPPROJECTIONS_HPP_

#include "map_msgs/msg/projected_map_info.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace map_msgs
{
namespace srv
{

struct SetMapProjections
{
  static constexpr const char * ros2_service_type = "map_msgs/srv/SetMapProjections";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "map_msgs/srv/SetMapProjections_Request";
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
      static constexpr const char * ros2_message_type = "map_msgs/srv/SetMapProjections_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _projected_maps_info_type = std::vector<::map_msgs::msg::ProjectedMapInfo>;
      std::vector<::map_msgs::msg::ProjectedMapInfo> projected_maps_info;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(projected_maps_info);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(projected_maps_info);
      }

      bool operator==(const Response & other) const
      {
        return projected_maps_info == other.projected_maps_info;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace map_msgs

#endif  // MAP_MSGS_SRV_SETMAPPROJECTIONS_HPP_
