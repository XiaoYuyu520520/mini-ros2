#ifndef PLUGINLIB__EXCEPTIONS_HPP_
#define PLUGINLIB__EXCEPTIONS_HPP_

#include <stdexcept>
#include <string>

namespace pluginlib
{

class PluginlibException : public std::runtime_error
{
public:
  explicit PluginlibException(const std::string & message)
  : std::runtime_error(message)
  {
  }
};

class CreateClassException : public PluginlibException
{
public:
  explicit CreateClassException(const std::string & message)
  : PluginlibException(message)
  {
  }
};

}  // namespace pluginlib

#endif  // PLUGINLIB__EXCEPTIONS_HPP_
