#ifndef SERVICE_MSGS_MSG_SERVICEEVENTINFO_HPP_
#define SERVICE_MSGS_MSG_SERVICEEVENTINFO_HPP_

#include <array>
#include "builtin_interfaces/msg/time.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace service_msgs
{
namespace msg
{

struct ServiceEventInfo
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "service_msgs/msg/ServiceEventInfo";
  using SharedPtr = std::shared_ptr<ServiceEventInfo>;
  using ConstSharedPtr = std::shared_ptr<const ServiceEventInfo>;
  using UniquePtr = std::unique_ptr<ServiceEventInfo>;
  static constexpr uint8_t REQUEST_SENT = 0;
  static constexpr uint8_t REQUEST_RECEIVED = 1;
  static constexpr uint8_t RESPONSE_SENT = 2;
  static constexpr uint8_t RESPONSE_RECEIVED = 3;

  using _event_type_type = uint8_t;
  uint8_t event_type = 0;
  using _stamp_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time stamp;
  using _client_gid_type = std::array<uint8_t, 16>;
  std::array<uint8_t, 16> client_gid = {};
  using _sequence_number_type = int64_t;
  int64_t sequence_number = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(event_type);
    ar(stamp);
    ar(client_gid);
    ar(sequence_number);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(event_type);
    ar(stamp);
    ar(client_gid);
    ar(sequence_number);
  }

  bool operator==(const ServiceEventInfo & other) const
  {
    return event_type == other.event_type &&
      stamp == other.stamp &&
      client_gid == other.client_gid &&
      sequence_number == other.sequence_number;
  }
  bool operator!=(const ServiceEventInfo & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace service_msgs

#endif  // SERVICE_MSGS_MSG_SERVICEEVENTINFO_HPP_
