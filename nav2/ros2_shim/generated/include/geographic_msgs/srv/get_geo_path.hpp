#ifndef GEOGRAPHIC_MSGS_SRV_GETGEOPATH_HPP_
#define GEOGRAPHIC_MSGS_SRV_GETGEOPATH_HPP_

#include "geographic_msgs/msg/geo_path.hpp"
#include "geographic_msgs/msg/geo_point.hpp"
#include "unique_identifier_msgs/msg/uuid.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geographic_msgs
{
namespace srv
{

struct GetGeoPath
{
  static constexpr const char * ros2_service_type = "geographic_msgs/srv/GetGeoPath";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "geographic_msgs/srv/GetGeoPath_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _start_type = ::geographic_msgs::msg::GeoPoint;
      ::geographic_msgs::msg::GeoPoint start;
      using _goal_type = ::geographic_msgs::msg::GeoPoint;
      ::geographic_msgs::msg::GeoPoint goal;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(start);
        ar(goal);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(start);
        ar(goal);
      }

      bool operator==(const Request & other) const
      {
        return start == other.start &&
          goal == other.goal;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "geographic_msgs/srv/GetGeoPath_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _success_type = bool;
      bool success = false;
      using _status_type = std::string;
      std::string status;
      using _plan_type = ::geographic_msgs::msg::GeoPath;
      ::geographic_msgs::msg::GeoPath plan;
      using _network_type = ::unique_identifier_msgs::msg::UUID;
      ::unique_identifier_msgs::msg::UUID network;
      using _start_seg_type = ::unique_identifier_msgs::msg::UUID;
      ::unique_identifier_msgs::msg::UUID start_seg;
      using _goal_seg_type = ::unique_identifier_msgs::msg::UUID;
      ::unique_identifier_msgs::msg::UUID goal_seg;
      using _distance_type = double;
      double distance = 0.0;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(success);
        ar(status);
        ar(plan);
        ar(network);
        ar(start_seg);
        ar(goal_seg);
        ar(distance);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(success);
        ar(status);
        ar(plan);
        ar(network);
        ar(start_seg);
        ar(goal_seg);
        ar(distance);
      }

      bool operator==(const Response & other) const
      {
        return success == other.success &&
          status == other.status &&
          plan == other.plan &&
          network == other.network &&
          start_seg == other.start_seg &&
          goal_seg == other.goal_seg &&
          distance == other.distance;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_SRV_GETGEOPATH_HPP_
