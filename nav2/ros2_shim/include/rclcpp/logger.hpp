#ifndef RCLCPP__LOGGER_HPP_
#define RCLCPP__LOGGER_HPP_

#include <string>

namespace rclcpp
{

class Logger
{
public:
  enum class Level
  {
    Unset = 0,
    Debug = 10,
    Info = 20,
    Warn = 30,
    Error = 40,
    Fatal = 50,
  };

  Logger() = default;

  Logger(const std::string & name, Level level = Level::Info)
  : name_(name), level_(level)
  {
  }

  const std::string & get_name() const {return name_;}

  Level get_level() const {return level_;}

  void set_level(Level level) {level_ = level;}

  Logger get_child(const std::string & suffix) const
  {
    return Logger(name_ + "." + suffix, level_);
  }

private:
  std::string name_ = "ros2_shim";
  Level level_ = Level::Info;
};

inline Logger get_logger(const std::string & name)
{
  return Logger(name);
}

}  // namespace rclcpp

#endif  // RCLCPP__LOGGER_HPP_
