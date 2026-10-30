#ifndef RCLCPP__EXCEPTIONS_HPP_
#define RCLCPP__EXCEPTIONS_HPP_

#include <stdexcept>
#include <string>

namespace rclcpp
{
namespace exceptions
{

class RCLCPPError : public std::runtime_error
{
public:
  explicit RCLCPPError(const std::string & message)
  : std::runtime_error(message)
  {
  }
};

class InvalidArgumentError : public RCLCPPError
{
public:
  explicit InvalidArgumentError(const std::string & message)
  : RCLCPPError(message)
  {
  }
};

class ParameterNotDeclaredException : public RCLCPPError
{
public:
  explicit ParameterNotDeclaredException(const std::string & name)
  : RCLCPPError("parameter '" + name + "' has not been declared")
  {
  }
};

class ParameterAlreadyDeclaredException : public RCLCPPError
{
public:
  explicit ParameterAlreadyDeclaredException(const std::string & name)
  : RCLCPPError("parameter '" + name + "' has already been declared")
  {
  }
};

class InvalidParameterValueException : public RCLCPPError
{
public:
  explicit InvalidParameterValueException(const std::string & message)
  : RCLCPPError(message)
  {
  }
};

class InvalidParameterTypeException : public RCLCPPError
{
public:
  explicit InvalidParameterTypeException(const std::string & message)
  : RCLCPPError(message)
  {
  }
};

class ParameterUninitializedException : public RCLCPPError
{
public:
  explicit ParameterUninitializedException(const std::string & name)
  : RCLCPPError("parameter '" + name + "' is not initialized")
  {
  }
};

class InvalidTopicNameError : public RCLCPPError
{
public:
  explicit InvalidTopicNameError(const std::string & name)
  : RCLCPPError("invalid topic name '" + name + "'")
  {
  }
};

}  // namespace exceptions
}  // namespace rclcpp

#endif  // RCLCPP__EXCEPTIONS_HPP_
