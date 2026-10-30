#ifndef SENSOR_MSGS_MSG_CAMERAINFO_HPP_
#define SENSOR_MSGS_MSG_CAMERAINFO_HPP_

#include <array>
#include "sensor_msgs/msg/region_of_interest.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct CameraInfo
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/CameraInfo";
  using SharedPtr = std::shared_ptr<CameraInfo>;
  using ConstSharedPtr = std::shared_ptr<const CameraInfo>;
  using UniquePtr = std::unique_ptr<CameraInfo>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _height_type = uint32_t;
  uint32_t height = 0;
  using _width_type = uint32_t;
  uint32_t width = 0;
  using _distortion_model_type = std::string;
  std::string distortion_model;
  using _d_type = std::vector<double>;
  std::vector<double> d;
  using _k_type = std::array<double, 9>;
  std::array<double, 9> k = {};
  using _r_type = std::array<double, 9>;
  std::array<double, 9> r = {};
  using _p_type = std::array<double, 12>;
  std::array<double, 12> p = {};
  using _binning_x_type = uint32_t;
  uint32_t binning_x = 0;
  using _binning_y_type = uint32_t;
  uint32_t binning_y = 0;
  using _roi_type = ::sensor_msgs::msg::RegionOfInterest;
  ::sensor_msgs::msg::RegionOfInterest roi;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(height);
    ar(width);
    ar(distortion_model);
    ar(d);
    ar(k);
    ar(r);
    ar(p);
    ar(binning_x);
    ar(binning_y);
    ar(roi);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(height);
    ar(width);
    ar(distortion_model);
    ar(d);
    ar(k);
    ar(r);
    ar(p);
    ar(binning_x);
    ar(binning_y);
    ar(roi);
  }

  bool operator==(const CameraInfo & other) const
  {
    return header == other.header &&
      height == other.height &&
      width == other.width &&
      distortion_model == other.distortion_model &&
      d == other.d &&
      k == other.k &&
      r == other.r &&
      p == other.p &&
      binning_x == other.binning_x &&
      binning_y == other.binning_y &&
      roi == other.roi;
  }
  bool operator!=(const CameraInfo & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_CAMERAINFO_HPP_
