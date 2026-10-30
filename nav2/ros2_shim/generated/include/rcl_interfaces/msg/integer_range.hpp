#ifndef RCL_INTERFACES_MSG_INTEGERRANGE_HPP_
#define RCL_INTERFACES_MSG_INTEGERRANGE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace msg
{

struct IntegerRange
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rcl_interfaces/msg/IntegerRange";
  using SharedPtr = std::shared_ptr<IntegerRange>;
  using ConstSharedPtr = std::shared_ptr<const IntegerRange>;
  using UniquePtr = std::unique_ptr<IntegerRange>;
  using _from_value_type = int64_t;
  int64_t from_value = 0;
  using _to_value_type = int64_t;
  int64_t to_value = 0;
  using _step_type = uint64_t;
  uint64_t step = 0;

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

  bool operator==(const IntegerRange & other) const
  {
    return from_value == other.from_value &&
      to_value == other.to_value &&
      step == other.step;
  }
  bool operator!=(const IntegerRange & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_MSG_INTEGERRANGE_HPP_
