#ifndef STD_MSGS_MSG_EMPTY_HPP_
#define STD_MSGS_MSG_EMPTY_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct Empty
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/Empty";
  using SharedPtr = std::shared_ptr<Empty>;
  using ConstSharedPtr = std::shared_ptr<const Empty>;
  using UniquePtr = std::unique_ptr<Empty>;

  template <class Archive>
  void visit(Archive & ar)
  {
    (void)ar;
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    (void)ar;
  }

  bool operator==(const Empty & other) const
  {
    (void)other;
    return true;
  }
  bool operator!=(const Empty & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_EMPTY_HPP_
