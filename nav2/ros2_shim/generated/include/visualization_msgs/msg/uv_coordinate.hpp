#ifndef VISUALIZATION_MSGS_MSG_UVCOORDINATE_HPP_
#define VISUALIZATION_MSGS_MSG_UVCOORDINATE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace visualization_msgs
{
namespace msg
{

struct UVCoordinate
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "visualization_msgs/msg/UVCoordinate";
  using SharedPtr = std::shared_ptr<UVCoordinate>;
  using ConstSharedPtr = std::shared_ptr<const UVCoordinate>;
  using UniquePtr = std::unique_ptr<UVCoordinate>;
  using _u_type = float;
  float u = 0.0f;
  using _v_type = float;
  float v = 0.0f;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(u);
    ar(v);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(u);
    ar(v);
  }

  bool operator==(const UVCoordinate & other) const
  {
    return u == other.u &&
      v == other.v;
  }
  bool operator!=(const UVCoordinate & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace visualization_msgs

#endif  // VISUALIZATION_MSGS_MSG_UVCOORDINATE_HPP_
