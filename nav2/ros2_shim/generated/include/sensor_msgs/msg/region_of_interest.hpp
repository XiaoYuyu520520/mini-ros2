#ifndef SENSOR_MSGS_MSG_REGIONOFINTEREST_HPP_
#define SENSOR_MSGS_MSG_REGIONOFINTEREST_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct RegionOfInterest
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/RegionOfInterest";
  using SharedPtr = std::shared_ptr<RegionOfInterest>;
  using ConstSharedPtr = std::shared_ptr<const RegionOfInterest>;
  using UniquePtr = std::unique_ptr<RegionOfInterest>;
  using _x_offset_type = uint32_t;
  uint32_t x_offset = 0;
  using _y_offset_type = uint32_t;
  uint32_t y_offset = 0;
  using _height_type = uint32_t;
  uint32_t height = 0;
  using _width_type = uint32_t;
  uint32_t width = 0;
  using _do_rectify_type = bool;
  bool do_rectify = false;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(x_offset);
    ar(y_offset);
    ar(height);
    ar(width);
    ar(do_rectify);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(x_offset);
    ar(y_offset);
    ar(height);
    ar(width);
    ar(do_rectify);
  }

  bool operator==(const RegionOfInterest & other) const
  {
    return x_offset == other.x_offset &&
      y_offset == other.y_offset &&
      height == other.height &&
      width == other.width &&
      do_rectify == other.do_rectify;
  }
  bool operator!=(const RegionOfInterest & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_REGIONOFINTEREST_HPP_
