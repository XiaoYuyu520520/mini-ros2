#ifndef RCL_INTERFACES_MSG_FLOATINGPOINTRANGE_HPP_
#define RCL_INTERFACES_MSG_FLOATINGPOINTRANGE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace msg
{

struct FloatingPointRange
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rcl_interfaces/msg/FloatingPointRange";
  using SharedPtr = std::shared_ptr<FloatingPointRange>;
  using ConstSharedPtr = std::shared_ptr<const FloatingPointRange>;
  using UniquePtr = std::unique_ptr<FloatingPointRange>;
  using _from_value_type = double;
  double from_value = 0.0;
  using _to_value_type = double;
  double to_value = 0.0;
  using _step_type = double;
  double step = 0.0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(from_value);
    ar(to_value);
    ar(step);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(from_value);
    ar(to_value);
    ar(step);
  }

  bool operator==(const FloatingPointRange & other) const
  {
    return from_value == other.from_value &&
      to_value == other.to_value &&
      step == other.step;
  }
  bool operator!=(const FloatingPointRange & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_MSG_FLOATINGPOINTRANGE_HPP_
