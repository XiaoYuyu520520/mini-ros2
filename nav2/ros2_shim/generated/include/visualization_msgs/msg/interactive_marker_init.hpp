#ifndef VISUALIZATION_MSGS_MSG_INTERACTIVEMARKERINIT_HPP_
#define VISUALIZATION_MSGS_MSG_INTERACTIVEMARKERINIT_HPP_

#include <vector>
#include "visualization_msgs/msg/interactive_marker.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace visualization_msgs
{
namespace msg
{

struct InteractiveMarkerInit
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "visualization_msgs/msg/InteractiveMarkerInit";
  using SharedPtr = std::shared_ptr<InteractiveMarkerInit>;
  using ConstSharedPtr = std::shared_ptr<const InteractiveMarkerInit>;
  using UniquePtr = std::unique_ptr<InteractiveMarkerInit>;
  using _server_id_type = std::string;
  std::string server_id;
  using _seq_num_type = uint64_t;
  uint64_t seq_num = 0;
  using _markers_type = std::vector<::visualization_msgs::msg::InteractiveMarker>;
  std::vector<::visualization_msgs::msg::InteractiveMarker> markers;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(server_id);
    ar(seq_num);
    ar(markers);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(server_id);
    ar(seq_num);
    ar(markers);
  }

  bool operator==(const InteractiveMarkerInit & other) const
  {
    return server_id == other.server_id &&
      seq_num == other.seq_num &&
      markers == other.markers;
  }
  bool operator!=(const InteractiveMarkerInit & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace visualization_msgs

#endif  // VISUALIZATION_MSGS_MSG_INTERACTIVEMARKERINIT_HPP_
