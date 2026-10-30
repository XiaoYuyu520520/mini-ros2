#ifndef DIAGNOSTIC_MSGS_MSG_DIAGNOSTICSTATUS_HPP_
#define DIAGNOSTIC_MSGS_MSG_DIAGNOSTICSTATUS_HPP_

#include "diagnostic_msgs/msg/key_value.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

// These constants share their names with macros a platform header defines
// (`ERROR` is one in `windows.h`), and a declaration is rewritten by the macro
// rather than compiled when one is in scope. Dropped here, once, instead of in
// every translation unit that includes both.
#ifdef ERROR
#  undef ERROR
#endif

namespace diagnostic_msgs
{
namespace msg
{

struct DiagnosticStatus
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "diagnostic_msgs/msg/DiagnosticStatus";
  using SharedPtr = std::shared_ptr<DiagnosticStatus>;
  using ConstSharedPtr = std::shared_ptr<const DiagnosticStatus>;
  using UniquePtr = std::unique_ptr<DiagnosticStatus>;
  static constexpr uint8_t OK = 0;
  static constexpr uint8_t WARN = 1;
  static constexpr uint8_t ERROR = 2;
  static constexpr uint8_t STALE = 3;

  using _level_type = uint8_t;
  uint8_t level = 0;
  using _name_type = std::string;
  std::string name;
  using _message_type = std::string;
  std::string message;
  using _hardware_id_type = std::string;
  std::string hardware_id;
  using _values_type = std::vector<::diagnostic_msgs::msg::KeyValue>;
  std::vector<::diagnostic_msgs::msg::KeyValue> values;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(level);
    ar(name);
    ar(message);
    ar(hardware_id);
    ar(values);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(level);
    ar(name);
    ar(message);
    ar(hardware_id);
    ar(values);
  }

  bool operator==(const DiagnosticStatus & other) const
  {
    return level == other.level &&
      name == other.name &&
      message == other.message &&
      hardware_id == other.hardware_id &&
      values == other.values;
  }
  bool operator!=(const DiagnosticStatus & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace diagnostic_msgs

#endif  // DIAGNOSTIC_MSGS_MSG_DIAGNOSTICSTATUS_HPP_
