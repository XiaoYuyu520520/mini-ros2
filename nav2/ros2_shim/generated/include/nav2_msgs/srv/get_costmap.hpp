#ifndef NAV2_MSGS_SRV_GETCOSTMAP_HPP_
#define NAV2_MSGS_SRV_GETCOSTMAP_HPP_

#include "nav2_msgs/msg/costmap.hpp"
#include "nav2_msgs/msg/costmap_meta_data.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace srv
{

struct GetCostmap
{
  static constexpr const char * ros2_service_type = "nav2_msgs/srv/GetCostmap";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/GetCostmap_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _specs_type = ::nav2_msgs::msg::CostmapMetaData;
      ::nav2_msgs::msg::CostmapMetaData specs;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(specs);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(specs);
      }

      bool operator==(const Request & other) const
      {
        return specs == other.specs;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/GetCostmap_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _map_type = ::nav2_msgs::msg::Costmap;
      ::nav2_msgs::msg::Costmap map;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(map);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(map);
      }

      bool operator==(const Response & other) const
      {
        return map == other.map;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_SRV_GETCOSTMAP_HPP_
