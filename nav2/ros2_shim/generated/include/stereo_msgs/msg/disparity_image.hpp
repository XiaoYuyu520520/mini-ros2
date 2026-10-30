#ifndef STEREO_MSGS_MSG_DISPARITYIMAGE_HPP_
#define STEREO_MSGS_MSG_DISPARITYIMAGE_HPP_

#include "sensor_msgs/msg/image.hpp"
#include "sensor_msgs/msg/region_of_interest.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace stereo_msgs
{
namespace msg
{

struct DisparityImage
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "stereo_msgs/msg/DisparityImage";
  using SharedPtr = std::shared_ptr<DisparityImage>;
  using ConstSharedPtr = std::shared_ptr<const DisparityImage>;
  using UniquePtr = std::unique_ptr<DisparityImage>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _image_type = ::sensor_msgs::msg::Image;
  ::sensor_msgs::msg::Image image;
  using _f_type = float;
  float f = 0.0f;
  using _t_type = float;
  float t = 0.0f;
  using _valid_window_type = ::sensor_msgs::msg::RegionOfInterest;
  ::sensor_msgs::msg::RegionOfInterest valid_window;
  using _min_disparity_type = float;
  float min_disparity = 0.0f;
  using _max_disparity_type = float;
  float max_disparity = 0.0f;
  using _delta_d_type = float;
  float delta_d = 0.0f;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(image);
    ar(f);
    ar(t);
    ar(valid_window);
    ar(min_disparity);
    ar(max_disparity);
    ar(delta_d);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(image);
    ar(f);
    ar(t);
    ar(valid_window);
    ar(min_disparity);
    ar(max_disparity);
    ar(delta_d);
  }

  bool operator==(const DisparityImage & other) const
  {
    return header == other.header &&
      image == other.image &&
      f == other.f &&
      t == other.t &&
      valid_window == other.valid_window &&
      min_disparity == other.min_disparity &&
      max_disparity == other.max_disparity &&
      delta_d == other.delta_d;
  }
  bool operator!=(const DisparityImage & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace stereo_msgs

#endif  // STEREO_MSGS_MSG_DISPARITYIMAGE_HPP_
