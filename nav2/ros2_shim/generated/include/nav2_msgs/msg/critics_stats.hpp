#ifndef NAV2_MSGS_MSG_CRITICSSTATS_HPP_
#define NAV2_MSGS_MSG_CRITICSSTATS_HPP_

#include "builtin_interfaces/msg/time.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct CriticsStats
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/CriticsStats";
  using SharedPtr = std::shared_ptr<CriticsStats>;
  using ConstSharedPtr = std::shared_ptr<const CriticsStats>;
  using UniquePtr = std::unique_ptr<CriticsStats>;
  using _stamp_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time stamp;
  using _critics_type = std::vector<std::string>;
  std::vector<std::string> critics;
  using _changed_type = std::vector<bool>;
  std::vector<bool> changed;
  using _costs_sum_type = std::vector<float>;
  std::vector<float> costs_sum;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(stamp);
    ar(critics);
    ar(changed);
    ar(costs_sum);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(stamp);
    ar(critics);
    ar(changed);
    ar(costs_sum);
  }

  bool operator==(const CriticsStats & other) const
  {
    return stamp == other.stamp &&
      critics == other.critics &&
      changed == other.changed &&
      costs_sum == other.costs_sum;
  }
  bool operator!=(const CriticsStats & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_CRITICSSTATS_HPP_
