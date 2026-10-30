#ifndef SENSOR_MSGS_MSG_IMAGE_HPP_
#define SENSOR_MSGS_MSG_IMAGE_HPP_

#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct Image
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/Image";
  using SharedPtr = std::shared_ptr<Image>;
  using ConstSharedPtr = std::shared_ptr<const Image>;
  using UniquePtr = std::unique_ptr<Image>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _height_type = uint32_t;
  uint32_t height = 0;
  using _width_type = uint32_t;
  uint32_t width = 0;
  using _encoding_type = std::string;
  std::string encoding;
  using _is_bigendian_type = uint8_t;
  uint8_t is_bigendian = 0;
  using _step_type = uint32_t;
  uint32_t step = 0;
  using _data_type = std::vector<uint8_t>;
  std::vector<uint8_t> data;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(height);
    ar(width);
    ar(encoding);
    ar(is_bigendian);
    ar(step);
    ar(data);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(height);
    ar(width);
    ar(encoding);
    ar(is_bigendian);
    ar(step);
    ar(data);
  }

  bool operator==(const Image & other) const
  {
    return header == other.header &&
      height == other.height &&
      width == other.width &&
      encoding == other.encoding &&
      is_bigendian == other.is_bigendian &&
      step == other.step &&
      data == other.data;
  }
  bool operator!=(const Image & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_IMAGE_HPP_
