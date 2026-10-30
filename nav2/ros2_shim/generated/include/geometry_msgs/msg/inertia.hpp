#ifndef GEOMETRY_MSGS_MSG_INERTIA_HPP_
#define GEOMETRY_MSGS_MSG_INERTIA_HPP_

#include "geometry_msgs/msg/vector3.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct Inertia
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/Inertia";
  using SharedPtr = std::shared_ptr<Inertia>;
  using ConstSharedPtr = std::shared_ptr<const Inertia>;
  using UniquePtr = std::unique_ptr<Inertia>;
  using _m_type = double;
  double m = 0.0;
  using _com_type = ::geometry_msgs::msg::Vector3;
  ::geometry_msgs::msg::Vector3 com;
  using _ixx_type = double;
  double ixx = 0.0;
  using _ixy_type = double;
  double ixy = 0.0;
  using _ixz_type = double;
  double ixz = 0.0;
  using _iyy_type = double;
  double iyy = 0.0;
  using _iyz_type = double;
  double iyz = 0.0;
  using _izz_type = double;
  double izz = 0.0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(m);
    ar(com);
    ar(ixx);
    ar(ixy);
    ar(ixz);
    ar(iyy);
    ar(iyz);
    ar(izz);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(m);
    ar(com);
    ar(ixx);
    ar(ixy);
    ar(ixz);
    ar(iyy);
    ar(iyz);
    ar(izz);
  }

  bool operator==(const Inertia & other) const
  {
    return m == other.m &&
      com == other.com &&
      ixx == other.ixx &&
      ixy == other.ixy &&
      ixz == other.ixz &&
      iyy == other.iyy &&
      iyz == other.iyz &&
      izz == other.izz;
  }
  bool operator!=(const Inertia & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_INERTIA_HPP_
