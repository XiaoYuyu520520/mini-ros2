#ifndef GEOGRAPHIC_MSGS_MSG_GEOGRAPHICMAP_HPP_
#define GEOGRAPHIC_MSGS_MSG_GEOGRAPHICMAP_HPP_

#include "geographic_msgs/msg/bounding_box.hpp"
#include "geographic_msgs/msg/key_value.hpp"
#include "geographic_msgs/msg/map_feature.hpp"
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

struct GeographicMap
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geographic_msgs/msg/GeographicMap";
  using SharedPtr = std::shared_ptr<GeographicMap>;
  using ConstSharedPtr = std::shared_ptr<const GeographicMap>;
  using UniquePtr = std::unique_ptr<GeographicMap>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _id_type = ::unique_identifier_msgs::msg::UUID;
  ::unique_identifier_msgs::msg::UUID id;
  using _bounds_type = ::geographic_msgs::msg::BoundingBox;
  ::geographic_msgs::msg::BoundingBox bounds;
  using _points_type = std::vector<::geographic_msgs::msg::WayPoint>;
  std::vector<::geographic_msgs::msg::WayPoint> points;
  using _features_type = std::vector<::geographic_msgs::msg::MapFeature>;
  std::vector<::geographic_msgs::msg::MapFeature> features;
  using _props_type = std::vector<::geographic_msgs::msg::KeyValue>;
  std::vector<::geographic_msgs::msg::KeyValue> props;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(id);
    ar(bounds);
    ar(points);
    ar(features);
    ar(props);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(id);
    ar(bounds);
    ar(points);
    ar(features);
    ar(props);
  }

  bool operator==(const GeographicMap & other) const
  {
    return header == other.header &&
      id == other.id &&
      bounds == other.bounds &&
      points == other.points &&
      features == other.features &&
      props == other.props;
  }
  bool operator!=(const GeographicMap & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_MSG_GEOGRAPHICMAP_HPP_
