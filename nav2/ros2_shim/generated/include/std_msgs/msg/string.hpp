#ifndef STD_MSGS_MSG_STRING_HPP_
#define STD_MSGS_MSG_STRING_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct String
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/String";
  using SharedPtr = std::shared_ptr<String>;
  using ConstSharedPtr = std::shared_ptr<const String>;
  using UniquePtr = std::unique_ptr<String>;
  using _data_type = std::string;
  std::string data;

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

  bool operator==(const String & other) const
  {
    return data == other.data;
  }
  bool operator!=(const String & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_STRING_HPP_
