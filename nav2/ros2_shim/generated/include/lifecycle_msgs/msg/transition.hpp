#ifndef LIFECYCLE_MSGS_MSG_TRANSITION_HPP_
#define LIFECYCLE_MSGS_MSG_TRANSITION_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace lifecycle_msgs
{
namespace msg
{

struct Transition
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "lifecycle_msgs/msg/Transition";
  using SharedPtr = std::shared_ptr<Transition>;
  using ConstSharedPtr = std::shared_ptr<const Transition>;
  using UniquePtr = std::unique_ptr<Transition>;
  static constexpr uint8_t TRANSITION_CREATE = 0;
  static constexpr uint8_t TRANSITION_CONFIGURE = 1;
  static constexpr uint8_t TRANSITION_CLEANUP = 2;
  static constexpr uint8_t TRANSITION_ACTIVATE = 3;
  static constexpr uint8_t TRANSITION_DEACTIVATE = 4;
  static constexpr uint8_t TRANSITION_UNCONFIGURED_SHUTDOWN = 5;
  static constexpr uint8_t TRANSITION_INACTIVE_SHUTDOWN = 6;
  static constexpr uint8_t TRANSITION_ACTIVE_SHUTDOWN = 7;
  static constexpr uint8_t TRANSITION_DESTROY = 8;
  static constexpr uint8_t TRANSITION_ON_CONFIGURE_SUCCESS = 10;
  static constexpr uint8_t TRANSITION_ON_CONFIGURE_FAILURE = 11;
  static constexpr uint8_t TRANSITION_ON_CONFIGURE_ERROR = 12;
  static constexpr uint8_t TRANSITION_ON_CLEANUP_SUCCESS = 20;
  static constexpr uint8_t TRANSITION_ON_CLEANUP_FAILURE = 21;
  static constexpr uint8_t TRANSITION_ON_CLEANUP_ERROR = 22;
  static constexpr uint8_t TRANSITION_ON_ACTIVATE_SUCCESS = 30;
  static constexpr uint8_t TRANSITION_ON_ACTIVATE_FAILURE = 31;
  static constexpr uint8_t TRANSITION_ON_ACTIVATE_ERROR = 32;
  static constexpr uint8_t TRANSITION_ON_DEACTIVATE_SUCCESS = 40;
  static constexpr uint8_t TRANSITION_ON_DEACTIVATE_FAILURE = 41;
  static constexpr uint8_t TRANSITION_ON_DEACTIVATE_ERROR = 42;
  static constexpr uint8_t TRANSITION_ON_SHUTDOWN_SUCCESS = 50;
  static constexpr uint8_t TRANSITION_ON_SHUTDOWN_FAILURE = 51;
  static constexpr uint8_t TRANSITION_ON_SHUTDOWN_ERROR = 52;
  static constexpr uint8_t TRANSITION_ON_ERROR_SUCCESS = 60;
  static constexpr uint8_t TRANSITION_ON_ERROR_FAILURE = 61;
  static constexpr uint8_t TRANSITION_ON_ERROR_ERROR = 62;
  static constexpr uint8_t TRANSITION_CALLBACK_SUCCESS = 97;
  static constexpr uint8_t TRANSITION_CALLBACK_FAILURE = 98;
  static constexpr uint8_t TRANSITION_CALLBACK_ERROR = 99;

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

  bool operator==(const Transition & other) const
  {
    return id == other.id &&
      label == other.label;
  }
  bool operator!=(const Transition & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace lifecycle_msgs

#endif  // LIFECYCLE_MSGS_MSG_TRANSITION_HPP_
