#ifndef RCL_INTERFACES_MSG_SETPARAMETERSRESULT_HPP_
#define RCL_INTERFACES_MSG_SETPARAMETERSRESULT_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace msg
{

struct SetParametersResult
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rcl_interfaces/msg/SetParametersResult";
  using SharedPtr = std::shared_ptr<SetParametersResult>;
  using ConstSharedPtr = std::shared_ptr<const SetParametersResult>;
  using UniquePtr = std::unique_ptr<SetParametersResult>;
  using _successful_type = bool;
  bool successful = false;
  using _reason_type = std::string;
  std::string reason;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(successful);
    ar(reason);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(successful);
    ar(reason);
  }

  bool operator==(const SetParametersResult & other) const
  {
    return successful == other.successful &&
      reason == other.reason;
  }
  bool operator!=(const SetParametersResult & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_MSG_SETPARAMETERSRESULT_HPP_
