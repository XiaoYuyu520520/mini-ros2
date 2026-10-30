#ifndef GEOGRAPHIC_MSGS_MSG_ROUTEPATH_HPP_
#define GEOGRAPHIC_MSGS_MSG_ROUTEPATH_HPP_

#include "geographic_msgs/msg/key_value.hpp"
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

struct RoutePath
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geographic_msgs/msg/RoutePath";
  using SharedPtr = std::shared_ptr<RoutePath>;
  using ConstSharedPtr = std::shared_ptr<const RoutePath>;
  using UniquePtr = std::unique_ptr<RoutePath>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _network_type = ::unique_identifier_msgs::msg::UUID;
  ::unique_identifier_msgs::msg::UUID network;
  using _segments_type = std::vector<::unique_identifier_msgs::msg::UUID>;
  std::vector<::unique_identifier_msgs::msg::UUID> segments;
  using _props_type = std::vector<::geographic_msgs::msg::KeyValue>;
  std::vector<::geographic_msgs::msg::KeyValue> props;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(network);
    ar(segments);
    ar(props);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(network);
    ar(segments);
    ar(props);
  }

  bool operator==(const RoutePath & other) const
  {
    return header == other.header &&
      network == other.network &&
      segments == other.segments &&
      props == other.props;
  }
  bool operator!=(const RoutePath & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_MSG_ROUTEPATH_HPP_
