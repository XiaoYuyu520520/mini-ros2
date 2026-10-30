#ifndef GEOGRAPHIC_MSGS_MSG_MAPFEATURE_HPP_
#define GEOGRAPHIC_MSGS_MSG_MAPFEATURE_HPP_

#include "geographic_msgs/msg/key_value.hpp"
#include "unique_identifier_msgs/msg/uuid.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace geographic_msgs
{
namespace msg
{

struct MapFeature
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geographic_msgs/msg/MapFeature";
  using SharedPtr = std::shared_ptr<MapFeature>;
  using ConstSharedPtr = std::shared_ptr<const MapFeature>;
  using UniquePtr = std::unique_ptr<MapFeature>;
  using _id_type = ::unique_identifier_msgs::msg::UUID;
  ::unique_identifier_msgs::msg::UUID id;
  using _components_type = std::vector<::unique_identifier_msgs::msg::UUID>;
  std::vector<::unique_identifier_msgs::msg::UUID> components;
  using _props_type = std::vector<::geographic_msgs::msg::KeyValue>;
  std::vector<::geographic_msgs::msg::KeyValue> props;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(id);
    ar(components);
    ar(props);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(id);
    ar(components);
    ar(props);
  }

  bool operator==(const MapFeature & other) const
  {
    return id == other.id &&
      components == other.components &&
      props == other.props;
  }
  bool operator!=(const MapFeature & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_MSG_MAPFEATURE_HPP_
