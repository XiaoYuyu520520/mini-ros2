#ifndef STD_MSGS_MSG_COLORRGBA_HPP_
#define STD_MSGS_MSG_COLORRGBA_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_msgs
{
namespace msg
{

struct ColorRGBA
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "std_msgs/msg/ColorRGBA";
  using SharedPtr = std::shared_ptr<ColorRGBA>;
  using ConstSharedPtr = std::shared_ptr<const ColorRGBA>;
  using UniquePtr = std::unique_ptr<ColorRGBA>;
  using _r_type = float;
  float r = 0.0f;
  using _g_type = float;
  float g = 0.0f;
  using _b_type = float;
  float b = 0.0f;
  using _a_type = float;
  float a = 0.0f;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(r);
    ar(g);
    ar(b);
    ar(a);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(r);
    ar(g);
    ar(b);
    ar(a);
  }

  bool operator==(const ColorRGBA & other) const
  {
    return r == other.r &&
      g == other.g &&
      b == other.b &&
      a == other.a;
  }
  bool operator!=(const ColorRGBA & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace std_msgs

#endif  // STD_MSGS_MSG_COLORRGBA_HPP_
