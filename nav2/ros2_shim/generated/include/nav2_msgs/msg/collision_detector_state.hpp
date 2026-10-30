#ifndef NAV2_MSGS_MSG_COLLISIONDETECTORSTATE_HPP_
#define NAV2_MSGS_MSG_COLLISIONDETECTORSTATE_HPP_

#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct CollisionDetectorState
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/CollisionDetectorState";
  using SharedPtr = std::shared_ptr<CollisionDetectorState>;
  using ConstSharedPtr = std::shared_ptr<const CollisionDetectorState>;
  using UniquePtr = std::unique_ptr<CollisionDetectorState>;
  using _polygons_type = std::vector<std::string>;
  std::vector<std::string> polygons;
  using _detections_type = std::vector<bool>;
  std::vector<bool> detections;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(polygons);
    ar(detections);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(polygons);
    ar(detections);
  }

  bool operator==(const CollisionDetectorState & other) const
  {
    return polygons == other.polygons &&
      detections == other.detections;
  }
  bool operator!=(const CollisionDetectorState & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_COLLISIONDETECTORSTATE_HPP_
