#ifndef NAV2_MSGS_MSG_COLLISIONMONITORSTATE_HPP_
#define NAV2_MSGS_MSG_COLLISIONMONITORSTATE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct CollisionMonitorState
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/CollisionMonitorState";
  using SharedPtr = std::shared_ptr<CollisionMonitorState>;
  using ConstSharedPtr = std::shared_ptr<const CollisionMonitorState>;
  using UniquePtr = std::unique_ptr<CollisionMonitorState>;
  static constexpr uint8_t DO_NOTHING = 0;
  static constexpr uint8_t STOP = 1;
  static constexpr uint8_t SLOWDOWN = 2;
  static constexpr uint8_t APPROACH = 3;
  static constexpr uint8_t LIMIT = 4;

  using _action_type_type = uint8_t;
  uint8_t action_type = 0;
  using _polygon_name_type = std::string;
  std::string polygon_name;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(action_type);
    ar(polygon_name);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(action_type);
    ar(polygon_name);
  }

  bool operator==(const CollisionMonitorState & other) const
  {
    return action_type == other.action_type &&
      polygon_name == other.polygon_name;
  }
  bool operator!=(const CollisionMonitorState & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_COLLISIONMONITORSTATE_HPP_
