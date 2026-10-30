#ifndef GEOGRAPHIC_MSGS_MSG_ROUTENETWORK_HPP_
#define GEOGRAPHIC_MSGS_MSG_ROUTENETWORK_HPP_

#include "geographic_msgs/msg/bounding_box.hpp"
#include "geographic_msgs/msg/key_value.hpp"
#include "geographic_msgs/msg/route_segment.hpp"
#include "geographic_msgs/msg/way_point.hpp"
#include "std_msgs/msg/header.hpp"
#include "unique_identifier_msgs/msg/uuid.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace geographic_msgs
{
namespace msg
{

struct RouteNetwork
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geographic_msgs/msg/RouteNetwork";
  using SharedPtr = std::shared_ptr<RouteNetwork>;
  using ConstSharedPtr = std::shared_ptr<const RouteNetwork>;
  using UniquePtr = std::unique_ptr<RouteNetwork>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _id_type = ::unique_identifier_msgs::msg::UUID;
  ::unique_identifier_msgs::msg::UUID id;
  using _bounds_type = ::geographic_msgs::msg::BoundingBox;
  ::geographic_msgs::msg::BoundingBox bounds;
  using _points_type = std::vector<::geographic_msgs::msg::WayPoint>;
  std::vector<::geographic_msgs::msg::WayPoint> points;
  using _segments_type = std::vector<::geographic_msgs::msg::RouteSegment>;
  std::vector<::geographic_msgs::msg::RouteSegment> segments;
  using _props_type = std::vector<::geographic_msgs::msg::KeyValue>;
  std::vector<::geographic_msgs::msg::KeyValue> props;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(id);
    ar(bounds);
    ar(points);
    ar(segments);
    ar(props);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(id);
    ar(bounds);
    ar(points);
    ar(segments);
    ar(props);
  }

  bool operator==(const RouteNetwork & other) const
  {
    return header == other.header &&
      id == other.id &&
      bounds == other.bounds &&
      points == other.points &&
      segments == other.segments &&
      props == other.props;
  }
  bool operator!=(const RouteNetwork & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_MSG_ROUTENETWORK_HPP_
