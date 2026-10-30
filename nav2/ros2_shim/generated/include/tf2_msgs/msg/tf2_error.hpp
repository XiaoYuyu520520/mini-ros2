#ifndef TF2_MSGS_MSG_TF2ERROR_HPP_
#define TF2_MSGS_MSG_TF2ERROR_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace tf2_msgs
{
namespace msg
{

struct TF2Error
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "tf2_msgs/msg/TF2Error";
  using SharedPtr = std::shared_ptr<TF2Error>;
  using ConstSharedPtr = std::shared_ptr<const TF2Error>;
  using UniquePtr = std::unique_ptr<TF2Error>;
  static constexpr uint8_t NO_ERROR = 0;
  static constexpr uint8_t LOOKUP_ERROR = 1;
  static constexpr uint8_t CONNECTIVITY_ERROR = 2;
  static constexpr uint8_t EXTRAPOLATION_ERROR = 3;
  static constexpr uint8_t INVALID_ARGUMENT_ERROR = 4;
  static constexpr uint8_t TIMEOUT_ERROR = 5;
  static constexpr uint8_t TRANSFORM_ERROR = 6;

  using _error_type = uint8_t;
  uint8_t error = 0;
  using _error_string_type = std::string;
  std::string error_string;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(error);
    ar(error_string);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(error);
    ar(error_string);
  }

  bool operator==(const TF2Error & other) const
  {
    return error == other.error &&
      error_string == other.error_string;
  }
  bool operator!=(const TF2Error & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace tf2_msgs

#endif  // TF2_MSGS_MSG_TF2ERROR_HPP_
