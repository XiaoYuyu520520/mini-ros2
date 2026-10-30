#ifndef LIFECYCLE_MSGS_MSG_STATE_HPP_
#define LIFECYCLE_MSGS_MSG_STATE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace lifecycle_msgs
{
namespace msg
{

struct State
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "lifecycle_msgs/msg/State";
  using SharedPtr = std::shared_ptr<State>;
  using ConstSharedPtr = std::shared_ptr<const State>;
  using UniquePtr = std::unique_ptr<State>;
  static constexpr uint8_t PRIMARY_STATE_UNKNOWN = 0;
  static constexpr uint8_t PRIMARY_STATE_UNCONFIGURED = 1;
  static constexpr uint8_t PRIMARY_STATE_INACTIVE = 2;
  static constexpr uint8_t PRIMARY_STATE_ACTIVE = 3;
  static constexpr uint8_t PRIMARY_STATE_FINALIZED = 4;
  static constexpr uint8_t TRANSITION_STATE_CONFIGURING = 10;
  static constexpr uint8_t TRANSITION_STATE_CLEANINGUP = 11;
  static constexpr uint8_t TRANSITION_STATE_SHUTTINGDOWN = 12;
  static constexpr uint8_t TRANSITION_STATE_ACTIVATING = 13;
  static constexpr uint8_t TRANSITION_STATE_DEACTIVATING = 14;
  static constexpr uint8_t TRANSITION_STATE_ERRORPROCESSING = 15;

  using _id_type = uint8_t;
  uint8_t id = 0;
  using _label_type = std::string;
  std::string label;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(id);
    ar(label);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(id);
    ar(label);
  }

  bool operator==(const State & other) const
  {
    return id == other.id &&
      label == other.label;
  }
  bool operator!=(const State & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace lifecycle_msgs

#endif  // LIFECYCLE_MSGS_MSG_STATE_HPP_
