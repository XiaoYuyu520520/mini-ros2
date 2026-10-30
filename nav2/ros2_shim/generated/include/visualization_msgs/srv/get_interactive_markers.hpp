#ifndef VISUALIZATION_MSGS_SRV_GETINTERACTIVEMARKERS_HPP_
#define VISUALIZATION_MSGS_SRV_GETINTERACTIVEMARKERS_HPP_

#include "vector"
#include "visualization_msgs/msg/interactive_marker.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace visualization_msgs
{
namespace srv
{

struct GetInteractiveMarkers
{
  static constexpr const char * ros2_service_type = "visualization_msgs/srv/GetInteractiveMarkers";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "visualization_msgs/srv/GetInteractiveMarkers_Request";
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
      static constexpr const char * ros2_message_type = "visualization_msgs/srv/GetInteractiveMarkers_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _sequence_number_type = uint64_t;
      uint64_t sequence_number = 0;
      using _markers_type = std::vector<::visualization_msgs::msg::InteractiveMarker>;
      std::vector<::visualization_msgs::msg::InteractiveMarker> markers;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(sequence_number);
        ar(markers);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(sequence_number);
        ar(markers);
      }

      bool operator==(const Response & other) const
      {
        return sequence_number == other.sequence_number &&
          markers == other.markers;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace visualization_msgs

#endif  // VISUALIZATION_MSGS_SRV_GETINTERACTIVEMARKERS_HPP_
