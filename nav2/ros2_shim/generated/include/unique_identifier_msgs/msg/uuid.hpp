#ifndef UNIQUE_IDENTIFIER_MSGS_MSG_UUID_HPP_
#define UNIQUE_IDENTIFIER_MSGS_MSG_UUID_HPP_

#include <array>

#include <cstdint>
#include <memory>
#include <string>

namespace unique_identifier_msgs
{
namespace msg
{

struct UUID
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "unique_identifier_msgs/msg/UUID";
  using SharedPtr = std::shared_ptr<UUID>;
  using ConstSharedPtr = std::shared_ptr<const UUID>;
  using UniquePtr = std::unique_ptr<UUID>;
  using _uuid_type = std::array<uint8_t, 16>;
  std::array<uint8_t, 16> uuid = {};

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(uuid);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(uuid);
  }

  bool operator==(const UUID & other) const
  {
    return uuid == other.uuid;
  }
  bool operator!=(const UUID & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace unique_identifier_msgs

#endif  // UNIQUE_IDENTIFIER_MSGS_MSG_UUID_HPP_
