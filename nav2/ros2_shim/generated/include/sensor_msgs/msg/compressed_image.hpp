#ifndef SENSOR_MSGS_MSG_COMPRESSEDIMAGE_HPP_
#define SENSOR_MSGS_MSG_COMPRESSEDIMAGE_HPP_

#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct CompressedImage
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/CompressedImage";
  using SharedPtr = std::shared_ptr<CompressedImage>;
  using ConstSharedPtr = std::shared_ptr<const CompressedImage>;
  using UniquePtr = std::unique_ptr<CompressedImage>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _format_type = std::string;
  std::string format;
  using _data_type = std::vector<uint8_t>;
  std::vector<uint8_t> data;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(format);
    ar(data);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(format);
    ar(data);
  }

  bool operator==(const CompressedImage & other) const
  {
    return header == other.header &&
      format == other.format &&
      data == other.data;
  }
  bool operator!=(const CompressedImage & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_COMPRESSEDIMAGE_HPP_
