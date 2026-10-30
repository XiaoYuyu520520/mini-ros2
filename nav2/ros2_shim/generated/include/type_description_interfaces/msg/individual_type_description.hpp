#ifndef TYPE_DESCRIPTION_INTERFACES_MSG_INDIVIDUALTYPEDESCRIPTION_HPP_
#define TYPE_DESCRIPTION_INTERFACES_MSG_INDIVIDUALTYPEDESCRIPTION_HPP_

#include "type_description_interfaces/msg/field.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace type_description_interfaces
{
namespace msg
{

struct IndividualTypeDescription
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "type_description_interfaces/msg/IndividualTypeDescription";
  using SharedPtr = std::shared_ptr<IndividualTypeDescription>;
  using ConstSharedPtr = std::shared_ptr<const IndividualTypeDescription>;
  using UniquePtr = std::unique_ptr<IndividualTypeDescription>;
  using _type_name_type = std::string;
  std::string type_name;
  using _fields_type = std::vector<::type_description_interfaces::msg::Field>;
  std::vector<::type_description_interfaces::msg::Field> fields;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(type_name);
    ar(fields);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(type_name);
    ar(fields);
  }

  bool operator==(const IndividualTypeDescription & other) const
  {
    return type_name == other.type_name &&
      fields == other.fields;
  }
  bool operator!=(const IndividualTypeDescription & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace type_description_interfaces

#endif  // TYPE_DESCRIPTION_INTERFACES_MSG_INDIVIDUALTYPEDESCRIPTION_HPP_
