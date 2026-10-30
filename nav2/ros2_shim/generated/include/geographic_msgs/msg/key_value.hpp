#ifndef GEOGRAPHIC_MSGS_MSG_KEYVALUE_HPP_
#define GEOGRAPHIC_MSGS_MSG_KEYVALUE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace geographic_msgs
{
namespace msg
{

struct KeyValue
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geographic_msgs/msg/KeyValue";
  using SharedPtr = std::shared_ptr<KeyValue>;
  using ConstSharedPtr = std::shared_ptr<const KeyValue>;
  using UniquePtr = std::unique_ptr<KeyValue>;
  using _key_type = std::string;
  std::string key;
  using _value_type = std::string;
  std::string value;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(key);
    ar(value);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(key);
    ar(value);
  }

  bool operator==(const KeyValue & other) const
  {
    return key == other.key &&
      value == other.value;
  }
  bool operator!=(const KeyValue & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_MSG_KEYVALUE_HPP_
