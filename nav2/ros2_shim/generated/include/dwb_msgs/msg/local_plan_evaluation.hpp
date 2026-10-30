#ifndef DWB_MSGS_MSG_LOCALPLANEVALUATION_HPP_
#define DWB_MSGS_MSG_LOCALPLANEVALUATION_HPP_

#include "dwb_msgs/msg/trajectory_score.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace dwb_msgs
{
namespace msg
{

struct LocalPlanEvaluation
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "dwb_msgs/msg/LocalPlanEvaluation";
  using SharedPtr = std::shared_ptr<LocalPlanEvaluation>;
  using ConstSharedPtr = std::shared_ptr<const LocalPlanEvaluation>;
  using UniquePtr = std::unique_ptr<LocalPlanEvaluation>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _twists_type = std::vector<::dwb_msgs::msg::TrajectoryScore>;
  std::vector<::dwb_msgs::msg::TrajectoryScore> twists;
  using _best_index_type = uint16_t;
  uint16_t best_index = 0;
  using _worst_index_type = uint16_t;
  uint16_t worst_index = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(twists);
    ar(best_index);
    ar(worst_index);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(twists);
    ar(best_index);
    ar(worst_index);
  }

  bool operator==(const LocalPlanEvaluation & other) const
  {
    return header == other.header &&
      twists == other.twists &&
      best_index == other.best_index &&
      worst_index == other.worst_index;
  }
  bool operator!=(const LocalPlanEvaluation & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace dwb_msgs

#endif  // DWB_MSGS_MSG_LOCALPLANEVALUATION_HPP_
