#ifndef NAV2_MSGS_MSG_EDGECOST_HPP_
#define NAV2_MSGS_MSG_EDGECOST_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct EdgeCost
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/EdgeCost";
  using SharedPtr = std::shared_ptr<EdgeCost>;
  using ConstSharedPtr = std::shared_ptr<const EdgeCost>;
  using UniquePtr = std::unique_ptr<EdgeCost>;
  using _edgeid_type = uint16_t;
  uint16_t edgeid = 0;
  using _cost_type = float;
  float cost = 0.0f;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(edgeid);
    ar(cost);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(edgeid);
    ar(cost);
  }

  bool operator==(const EdgeCost & other) const
  {
    return edgeid == other.edgeid &&
      cost == other.cost;
  }
  bool operator!=(const EdgeCost & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_EDGECOST_HPP_
