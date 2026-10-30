#ifndef DWB_MSGS_MSG_TRAJECTORYSCORE_HPP_
#define DWB_MSGS_MSG_TRAJECTORYSCORE_HPP_

#include "dwb_msgs/msg/critic_score.hpp"
#include "dwb_msgs/msg/trajectory2_d.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace dwb_msgs
{
namespace msg
{

struct TrajectoryScore
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "dwb_msgs/msg/TrajectoryScore";
  using SharedPtr = std::shared_ptr<TrajectoryScore>;
  using ConstSharedPtr = std::shared_ptr<const TrajectoryScore>;
  using UniquePtr = std::unique_ptr<TrajectoryScore>;
  using _traj_type = ::dwb_msgs::msg::Trajectory2D;
  ::dwb_msgs::msg::Trajectory2D traj;
  using _scores_type = std::vector<::dwb_msgs::msg::CriticScore>;
  std::vector<::dwb_msgs::msg::CriticScore> scores;
  using _total_type = float;
  float total = 0.0f;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(traj);
    ar(scores);
    ar(total);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(traj);
    ar(scores);
    ar(total);
  }

  bool operator==(const TrajectoryScore & other) const
  {
    return traj == other.traj &&
      scores == other.scores &&
      total == other.total;
  }
  bool operator!=(const TrajectoryScore & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace dwb_msgs

#endif  // DWB_MSGS_MSG_TRAJECTORYSCORE_HPP_
