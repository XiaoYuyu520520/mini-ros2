#ifndef TYPE_DESCRIPTION_INTERFACES_MSG_TYPEDESCRIPTION_HPP_
#define TYPE_DESCRIPTION_INTERFACES_MSG_TYPEDESCRIPTION_HPP_

#include "type_description_interfaces/msg/individual_type_description.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace type_description_interfaces
{
namespace msg
{

struct TypeDescription
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "type_description_interfaces/msg/TypeDescription";
  using SharedPtr = std::shared_ptr<TypeDescription>;
  using ConstSharedPtr = std::shared_ptr<const TypeDescription>;
  using UniquePtr = std::unique_ptr<TypeDescription>;
  using _type_description_type = ::type_description_interfaces::msg::IndividualTypeDescription;
  ::type_description_interfaces::msg::IndividualTypeDescription type_description;
  using _referenced_type_descriptions_type = std::vector<::type_description_interfaces::msg::IndividualTypeDescription>;
  std::vector<::type_description_interfaces::msg::IndividualTypeDescription> referenced_type_descriptions;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(type_description);
    ar(referenced_type_descriptions);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(type_description);
    ar(referenced_type_descriptions);
  }

  bool operator==(const TypeDescription & other) const
  {
    return type_description == other.type_description &&
      referenced_type_descriptions == other.referenced_type_descriptions;
  }
  bool operator!=(const TypeDescription & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace type_description_interfaces

#endif  // TYPE_DESCRIPTION_INTERFACES_MSG_TYPEDESCRIPTION_HPP_
