#ifndef TYPE_DESCRIPTION_INTERFACES_MSG_TYPESOURCE_HPP_
#define TYPE_DESCRIPTION_INTERFACES_MSG_TYPESOURCE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace type_description_interfaces
{
namespace msg
{

struct TypeSource
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "type_description_interfaces/msg/TypeSource";
  using SharedPtr = std::shared_ptr<TypeSource>;
  using ConstSharedPtr = std::shared_ptr<const TypeSource>;
  using UniquePtr = std::unique_ptr<TypeSource>;
  using _type_name_type = std::string;
  std::string type_name;
  using _encoding_type = std::string;
  std::string encoding;
  using _raw_file_contents_type = std::string;
  std::string raw_file_contents;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(type_name);
    ar(encoding);
    ar(raw_file_contents);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(type_name);
    ar(encoding);
    ar(raw_file_contents);
  }

  bool operator==(const TypeSource & other) const
  {
    return type_name == other.type_name &&
      encoding == other.encoding &&
      raw_file_contents == other.raw_file_contents;
  }
  bool operator!=(const TypeSource & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace type_description_interfaces

#endif  // TYPE_DESCRIPTION_INTERFACES_MSG_TYPESOURCE_HPP_
