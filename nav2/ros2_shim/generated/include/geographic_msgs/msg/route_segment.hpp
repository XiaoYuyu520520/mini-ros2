#ifndef GEOGRAPHIC_MSGS_MSG_ROUTESEGMENT_HPP_
#define GEOGRAPHIC_MSGS_MSG_ROUTESEGMENT_HPP_

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

struct RouteSegment
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geographic_msgs/msg/RouteSegment";
  using SharedPtr = std::shared_ptr<RouteSegment>;
  using ConstSharedPtr = std::shared_ptr<const RouteSegment>;
  using UniquePtr = std::unique_ptr<RouteSegment>;
  using _id_type = ::unique_identifier_msgs::msg::UUID;
  ::unique_identifier_msgs::msg::UUID id;
  using _start_type = ::unique_identifier_msgs::msg::UUID;
  ::unique_identifier_msgs::msg::UUID start;
  using _end_type = ::unique_identifier_msgs::msg::UUID;
  ::unique_identifier_msgs::msg::UUID end;
  using _props_type = std::vector<::geographic_msgs::msg::KeyValue>;
  std::vector<::geographic_msgs::msg::KeyValue> props;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(id);
    ar(start);
    ar(end);
    ar(props);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(id);
    ar(start);
    ar(end);
    ar(props);
  }

  bool operator==(const RouteSegment & other) const
  {
    return id == other.id &&
      start == other.start &&
      end == other.end &&
      props == other.props;
  }
  bool operator!=(const RouteSegment & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_MSG_ROUTESEGMENT_HPP_
