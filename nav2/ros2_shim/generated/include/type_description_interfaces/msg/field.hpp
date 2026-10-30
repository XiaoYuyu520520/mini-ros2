#ifndef TYPE_DESCRIPTION_INTERFACES_MSG_FIELD_HPP_
#define TYPE_DESCRIPTION_INTERFACES_MSG_FIELD_HPP_

#include "type_description_interfaces/msg/field_type.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace type_description_interfaces
{
namespace msg
{

struct Field
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "type_description_interfaces/msg/Field";
  using SharedPtr = std::shared_ptr<Field>;
  using ConstSharedPtr = std::shared_ptr<const Field>;
  using UniquePtr = std::unique_ptr<Field>;
  using _name_type = std::string;
  std::string name;
  using _type_type = ::type_description_interfaces::msg::FieldType;
  ::type_description_interfaces::msg::FieldType type;
  using _default_value_type = std::string;
  std::string default_value;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(name);
    ar(type);
    ar(default_value);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(name);
    ar(type);
    ar(default_value);
  }

  bool operator==(const Field & other) const
  {
    return name == other.name &&
      type == other.type &&
      default_value == other.default_value;
  }
  bool operator!=(const Field & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace type_description_interfaces

#endif  // TYPE_DESCRIPTION_INTERFACES_MSG_FIELD_HPP_
