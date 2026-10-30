#ifndef DIAGNOSTIC_MSGS_MSG_DIAGNOSTICARRAY_HPP_
#define DIAGNOSTIC_MSGS_MSG_DIAGNOSTICARRAY_HPP_

#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace diagnostic_msgs
{
namespace msg
{

struct DiagnosticArray
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "diagnostic_msgs/msg/DiagnosticArray";
  using SharedPtr = std::shared_ptr<DiagnosticArray>;
  using ConstSharedPtr = std::shared_ptr<const DiagnosticArray>;
  using UniquePtr = std::unique_ptr<DiagnosticArray>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _status_type = std::vector<::diagnostic_msgs::msg::DiagnosticStatus>;
  std::vector<::diagnostic_msgs::msg::DiagnosticStatus> status;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(status);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(status);
  }

  bool operator==(const DiagnosticArray & other) const
  {
    return header == other.header &&
      status == other.status;
  }
  bool operator!=(const DiagnosticArray & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace diagnostic_msgs

#endif  // DIAGNOSTIC_MSGS_MSG_DIAGNOSTICARRAY_HPP_
