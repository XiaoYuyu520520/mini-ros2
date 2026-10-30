#ifndef VISUALIZATION_MSGS_MSG_MENUENTRY_HPP_
#define VISUALIZATION_MSGS_MSG_MENUENTRY_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace visualization_msgs
{
namespace msg
{

struct MenuEntry
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "visualization_msgs/msg/MenuEntry";
  using SharedPtr = std::shared_ptr<MenuEntry>;
  using ConstSharedPtr = std::shared_ptr<const MenuEntry>;
  using UniquePtr = std::unique_ptr<MenuEntry>;
  static constexpr uint8_t FEEDBACK = 0;
  static constexpr uint8_t ROSRUN = 1;
  static constexpr uint8_t ROSLAUNCH = 2;

  using _id_type = uint32_t;
  uint32_t id = 0;
  using _parent_id_type = uint32_t;
  uint32_t parent_id = 0;
  using _title_type = std::string;
  std::string title;
  using _command_type = std::string;
  std::string command;
  using _command_type_type = uint8_t;
  uint8_t command_type = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(id);
    ar(parent_id);
    ar(title);
    ar(command);
    ar(command_type);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(id);
    ar(parent_id);
    ar(title);
    ar(command);
    ar(command_type);
  }

  bool operator==(const MenuEntry & other) const
  {
    return id == other.id &&
      parent_id == other.parent_id &&
      title == other.title &&
      command == other.command &&
      command_type == other.command_type;
  }
  bool operator!=(const MenuEntry & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace visualization_msgs

#endif  // VISUALIZATION_MSGS_MSG_MENUENTRY_HPP_
