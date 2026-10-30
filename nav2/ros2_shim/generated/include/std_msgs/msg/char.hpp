#ifndef STD_MSGS_MSG_CHAR_HPP_
#define STD_MSGS_MSG_CHAR_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct Char
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/Char";
  using SharedPtr = std::shared_ptr<Char>;
  using ConstSharedPtr = std::shared_ptr<const Char>;
  using UniquePtr = std::unique_ptr<Char>;
  using _data_type = uint8_t;
  uint8_t data = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(data);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(data);
  }

  bool operator==(const Char & other) const
  {
    return data == other.data;
  }
  bool operator!=(const Char & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_CHAR_HPP_
