#ifndef ROS2_BRIDGE_TRANSCODE_HPP
#define ROS2_BRIDGE_TRANSCODE_HPP

// The two conversions every registry in this directory is built from: between
// the shim's own compact layout (nav2/ros2_shim/include/ros2_shim/
// serialization.hpp) and real ROS2 DDS-CDR, for one generated message type.
//
// Both directions plug into the `visit()` method
// nav2/ros2_shim/tools/generate_messages.py emits for every message *and* for
// the two halves of every service, so the same template transcodes topics and
// service payloads alike (see type_registry.cpp and service_registry.cpp).

#include <cstddef>
#include <string>

#include "ros2_bridge/cdr_archive.hpp"
#include "ros2_bridge/type_registry.hpp"
#include "ros2_shim/serialization.hpp"

namespace ros2_bridge
{

// A CDR payload is the 4 byte encapsulation header plus a body, and Fast CDR
// refuses to read one whose body is empty: measured against the robot (ROS2
// Jazzy, rmw_fastrtps_cpp), `rmw_deserialize` of `00 01 00 00` - the whole
// encoding of a message with no fields - fails with "Fast CDR exception
// deserializing message", while the same payload with even one body byte reads
// back fine. `rmw_serialize` never produces that empty body in the first place:
// it pads a field-less message out to four zero body bytes, i.e.
// `00 01 00 00 00 00 00 00`. This does the same rather than inventing a layout.
//
// Field-less messages are not a corner case here: the `std_srvs/srv/Trigger`
// and `std_srvs/srv/Empty` requests, and nav2`s `GetShapes` and
// `ClearEntireCostmap` requests, have no fields at all, so without the padding
// the far end rejects every call to them with an error that names the *type*,
// not the length.
constexpr std::size_t kCdrHeaderSize = 4;
constexpr std::size_t kCdrEmptyBodyPadding = 4;

// shim layout -> CDR, used when publishing towards a remote agent.
template<class Message>
bool shimToCdr(const Bytes & shim, Bytes & cdr)
{
  Message message;
  if (!ros2_shim::deserialize(shim.data(), shim.size(), message)) {
    return false;
  }
  ros2_grpc_sdk::cdr::Writer writer;
  CdrWriteArchive archive(writer);
  message.visit(archive);
  cdr = writer.take();
  if (cdr.size() == kCdrHeaderSize) {
    cdr.resize(kCdrHeaderSize + kCdrEmptyBodyPadding, 0);
  }
  return true;
}

// CDR -> shim layout, used when mirroring a remote message into shared memory.
template<class Message>
bool cdrToShim(const Bytes & cdr, Bytes & shim)
{
  CdrReadArchive archive(cdr.data(), cdr.size());
  Message message;
  message.visit(archive);
  if (!archive.ok()) {
    return false;
  }
  shim = ros2_shim::serialize(message);
  return true;
}

}  // namespace ros2_bridge

#endif  // ROS2_BRIDGE_TRANSCODE_HPP
