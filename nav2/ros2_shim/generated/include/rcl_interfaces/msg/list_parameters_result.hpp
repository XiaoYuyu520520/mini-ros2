#ifndef RCL_INTERFACES_MSG_LISTPARAMETERSRESULT_HPP_
#define RCL_INTERFACES_MSG_LISTPARAMETERSRESULT_HPP_

#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace msg
{

struct ListParametersResult
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rcl_interfaces/msg/ListParametersResult";
  using SharedPtr = std::shared_ptr<ListParametersResult>;
  using ConstSharedPtr = std::shared_ptr<const ListParametersResult>;
  using UniquePtr = std::unique_ptr<ListParametersResult>;
  using _names_type = std::vector<std::string>;
  std::vector<std::string> names;
  using _prefixes_type = std::vector<std::string>;
  std::vector<std::string> prefixes;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(names);
    ar(prefixes);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(names);
    ar(prefixes);
  }

  bool operator==(const ListParametersResult & other) const
  {
    return names == other.names &&
      prefixes == other.prefixes;
  }
  bool operator!=(const ListParametersResult & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_MSG_LISTPARAMETERSRESULT_HPP_
