#ifndef RCL_INTERFACES_MSG_LOG_HPP_
#define RCL_INTERFACES_MSG_LOG_HPP_

#include "builtin_interfaces/msg/time.hpp"

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

namespace rcl_interfaces
{
namespace msg
{

struct Log
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rcl_interfaces/msg/Log";
  using SharedPtr = std::shared_ptr<Log>;
  using ConstSharedPtr = std::shared_ptr<const Log>;
  using UniquePtr = std::unique_ptr<Log>;
  static constexpr uint8_t DEBUG = 10;
  static constexpr uint8_t INFO = 20;
  static constexpr uint8_t WARN = 30;
  static constexpr uint8_t ERROR = 40;
  static constexpr uint8_t FATAL = 50;

  using _stamp_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time stamp;
  using _level_type = uint8_t;
  uint8_t level = 0;
  using _name_type = std::string;
  std::string name;
  using _msg_type = std::string;
  std::string msg;
  using _file_type = std::string;
  std::string file;
  using _function_type = std::string;
  std::string function;
  using _line_type = uint32_t;
  uint32_t line = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(stamp);
    ar(level);
    ar(name);
    ar(msg);
    ar(file);
    ar(function);
    ar(line);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(stamp);
    ar(level);
    ar(name);
    ar(msg);
    ar(file);
    ar(function);
    ar(line);
  }

  bool operator==(const Log & other) const
  {
    return stamp == other.stamp &&
      level == other.level &&
      name == other.name &&
      msg == other.msg &&
      file == other.file &&
      function == other.function &&
      line == other.line;
  }
  bool operator!=(const Log & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_MSG_LOG_HPP_
