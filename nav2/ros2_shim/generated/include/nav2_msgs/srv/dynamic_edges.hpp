#ifndef NAV2_MSGS_SRV_DYNAMICEDGES_HPP_
#define NAV2_MSGS_SRV_DYNAMICEDGES_HPP_

#include "nav2_msgs/msg/edge_cost.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace srv
{

struct DynamicEdges
{
  static constexpr const char * ros2_service_type = "nav2_msgs/srv/DynamicEdges";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/DynamicEdges_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _closed_edges_type = std::vector<uint16_t>;
      std::vector<uint16_t> closed_edges;
      using _opened_edges_type = std::vector<uint16_t>;
      std::vector<uint16_t> opened_edges;
      using _adjust_edges_type = std::vector<::nav2_msgs::msg::EdgeCost>;
      std::vector<::nav2_msgs::msg::EdgeCost> adjust_edges;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(closed_edges);
        ar(opened_edges);
        ar(adjust_edges);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(closed_edges);
        ar(opened_edges);
        ar(adjust_edges);
      }

      bool operator==(const Request & other) const
      {
        return closed_edges == other.closed_edges &&
          opened_edges == other.opened_edges &&
          adjust_edges == other.adjust_edges;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/DynamicEdges_Response";
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

#endif  // NAV2_MSGS_SRV_DYNAMICEDGES_HPP_
