#ifndef ROSGRAPH_MSGS_MSG_TYPEHASH_HPP_
#define ROSGRAPH_MSGS_MSG_TYPEHASH_HPP_

#include <array>

#include <cstdint>
#include <memory>
#include <string>

namespace rosgraph_msgs
{
namespace msg
{

struct TypeHash
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rosgraph_msgs/msg/TypeHash";
  using SharedPtr = std::shared_ptr<TypeHash>;
  using ConstSharedPtr = std::shared_ptr<const TypeHash>;
  using UniquePtr = std::unique_ptr<TypeHash>;
  using _version_type = uint8_t;
  uint8_t version = 0;
  using _value_type = std::array<uint8_t, 32>;
  std::array<uint8_t, 32> value = {};

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(version);
    ar(value);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(version);
    ar(value);
  }

  bool operator==(const TypeHash & other) const
  {
    return version == other.version &&
      value == other.value;
  }
  bool operator!=(const TypeHash & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rosgraph_msgs

#endif  // ROSGRAPH_MSGS_MSG_TYPEHASH_HPP_
