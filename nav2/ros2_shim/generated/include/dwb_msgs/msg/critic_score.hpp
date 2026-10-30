#ifndef DWB_MSGS_MSG_CRITICSCORE_HPP_
#define DWB_MSGS_MSG_CRITICSCORE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace dwb_msgs
{
namespace msg
{

struct CriticScore
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "dwb_msgs/msg/CriticScore";
  using SharedPtr = std::shared_ptr<CriticScore>;
  using ConstSharedPtr = std::shared_ptr<const CriticScore>;
  using UniquePtr = std::unique_ptr<CriticScore>;
  using _name_type = std::string;
  std::string name;
  using _raw_score_type = float;
  float raw_score = 0.0f;
  using _scale_type = float;
  float scale = 0.0f;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(name);
    ar(raw_score);
    ar(scale);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(name);
    ar(raw_score);
    ar(scale);
  }

  bool operator==(const CriticScore & other) const
  {
    return name == other.name &&
      raw_score == other.raw_score &&
      scale == other.scale;
  }
  bool operator!=(const CriticScore & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace dwb_msgs

#endif  // DWB_MSGS_MSG_CRITICSCORE_HPP_
