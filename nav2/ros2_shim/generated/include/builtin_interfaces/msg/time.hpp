#ifndef BUILTIN_INTERFACES_MSG_TIME_HPP_
#define BUILTIN_INTERFACES_MSG_TIME_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace builtin_interfaces
{
namespace msg
{

struct Time
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "builtin_interfaces/msg/Time";
  using SharedPtr = std::shared_ptr<Time>;
  using ConstSharedPtr = std::shared_ptr<const Time>;
  using UniquePtr = std::unique_ptr<Time>;
  using _sec_type = int32_t;
  int32_t sec = 0;
  using _nanosec_type = uint32_t;
  uint32_t nanosec = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(sec);
    ar(nanosec);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(sec);
    ar(nanosec);
  }

  bool operator==(const Time & other) const
  {
    return sec == other.sec &&
      nanosec == other.nanosec;
  }
  bool operator!=(const Time & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace builtin_interfaces

#endif  // BUILTIN_INTERFACES_MSG_TIME_HPP_
