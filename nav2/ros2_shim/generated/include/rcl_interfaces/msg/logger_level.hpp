#ifndef RCL_INTERFACES_MSG_LOGGERLEVEL_HPP_
#define RCL_INTERFACES_MSG_LOGGERLEVEL_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace msg
{

struct LoggerLevel
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rcl_interfaces/msg/LoggerLevel";
  using SharedPtr = std::shared_ptr<LoggerLevel>;
  using ConstSharedPtr = std::shared_ptr<const LoggerLevel>;
  using UniquePtr = std::unique_ptr<LoggerLevel>;
  static constexpr uint8_t LOG_LEVEL_UNKNOWN = 0;
  static constexpr uint8_t LOG_LEVEL_DEBUG = 10;
  static constexpr uint8_t LOG_LEVEL_INFO = 20;
  static constexpr uint8_t LOG_LEVEL_WARN = 30;
  static constexpr uint8_t LOG_LEVEL_ERROR = 40;
  static constexpr uint8_t LOG_LEVEL_FATAL = 50;

  using _name_type = std::string;
  std::string name;
  using _level_type = uint32_t;
  uint32_t level = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(name);
    ar(level);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(name);
    ar(level);
  }

  bool operator==(const LoggerLevel & other) const
  {
    return name == other.name &&
      level == other.level;
  }
  bool operator!=(const LoggerLevel & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_MSG_LOGGERLEVEL_HPP_
