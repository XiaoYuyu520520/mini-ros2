#ifndef VISUALIZATION_MSGS_MSG_INTERACTIVEMARKERUPDATE_HPP_
#define VISUALIZATION_MSGS_MSG_INTERACTIVEMARKERUPDATE_HPP_

#include <vector>
#include "visualization_msgs/msg/interactive_marker.hpp"
#include "visualization_msgs/msg/interactive_marker_pose.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace visualization_msgs
{
namespace msg
{

struct InteractiveMarkerUpdate
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "visualization_msgs/msg/InteractiveMarkerUpdate";
  using SharedPtr = std::shared_ptr<InteractiveMarkerUpdate>;
  using ConstSharedPtr = std::shared_ptr<const InteractiveMarkerUpdate>;
  using UniquePtr = std::unique_ptr<InteractiveMarkerUpdate>;
  static constexpr uint8_t KEEP_ALIVE = 0;
  static constexpr uint8_t UPDATE = 1;

  using _server_id_type = std::string;
  std::string server_id;
  using _seq_num_type = uint64_t;
  uint64_t seq_num = 0;
  using _type_type = uint8_t;
  uint8_t type = 0;
  using _markers_type = std::vector<::visualization_msgs::msg::InteractiveMarker>;
  std::vector<::visualization_msgs::msg::InteractiveMarker> markers;
  using _poses_type = std::vector<::visualization_msgs::msg::InteractiveMarkerPose>;
  std::vector<::visualization_msgs::msg::InteractiveMarkerPose> poses;
  using _erases_type = std::vector<std::string>;
  std::vector<std::string> erases;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(server_id);
    ar(seq_num);
    ar(type);
    ar(markers);
    ar(poses);
    ar(erases);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(server_id);
    ar(seq_num);
    ar(type);
    ar(markers);
    ar(poses);
    ar(erases);
  }

  bool operator==(const InteractiveMarkerUpdate & other) const
  {
    return server_id == other.server_id &&
      seq_num == other.seq_num &&
      type == other.type &&
      markers == other.markers &&
      poses == other.poses &&
      erases == other.erases;
  }
  bool operator!=(const InteractiveMarkerUpdate & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace visualization_msgs

#endif  // VISUALIZATION_MSGS_MSG_INTERACTIVEMARKERUPDATE_HPP_
