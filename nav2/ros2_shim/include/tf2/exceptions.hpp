#ifndef TF2__EXCEPTIONS_HPP_
#define TF2__EXCEPTIONS_HPP_

#include <stdexcept>
#include <string>

namespace tf2
{

class TransformException : public std::runtime_error
{
public:
  explicit TransformException(const std::string & message)
  : std::runtime_error(message)
  {
  }
};

class LookupException : public TransformException
{
public:
  explicit LookupException(const std::string & message)
  : TransformException(message)
  {
  }
};

class ConnectivityException : public TransformException
{
public:
  explicit ConnectivityException(const std::string & message)
  : TransformException(message)
  {
  }
};

class ExtrapolationException : public TransformException
{
public:
  explicit ExtrapolationException(const std::string & message)
  : TransformException(message)
  {
  }
};

class InvalidArgumentException : public TransformException
{
public:
  explicit InvalidArgumentException(const std::string & message)
  : TransformException(message)
  {
  }
};

class TimeoutException : public TransformException
{
public:
  explicit TimeoutException(const std::string & message)
  : TransformException(message)
  {
  }
};

}  // namespace tf2

#endif  // TF2__EXCEPTIONS_HPP_
