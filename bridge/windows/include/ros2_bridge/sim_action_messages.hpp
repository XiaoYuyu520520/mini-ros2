#ifndef ROS2_BRIDGE_SIM_ACTION_MESSAGES_HPP
#define ROS2_BRIDGE_SIM_ACTION_MESSAGES_HPP

// The goal/feedback/result of `ros2_bridge_sim/action/Drive`, written in the
// same shape nav2/ros2_shim/tools/generate_messages.py emits for every ROS2
// message of the ported packages.
//
// Why hand written: the shim only generates structs for the packages nav2
// compiles against, and the simulated robot's own package is not one of them -
// it exists on the Ubuntu side as a Python module (bridge/ubuntu/
// ros2_bridge_sim/messages.py) and nowhere else. The bridge still has to be
// able to *transcode* it, because that is how `/sim/drive` reaches a caller on
// Windows: the goal travels as CDR over gRPC and as the shim's layout in
// shared memory, and both conversions go through `visit()`.
//
// The three definitions below are a translation of the schema table in
// bridge/ubuntu/ros2_bridge_sim/messages.py (DRIVE_GOAL / DRIVE_FEEDBACK /
// DRIVE_RESULT), field for field and in declaration order - the shim's layout
// is just the fields back to back (see ros2_shim/serialization.hpp), so the
// order is the wire format. Adding an action to the bridge means adding its
// structs here plus an entry in src/action_registry.cpp.

#include <cstdint>
#include <memory>
#include <string>

namespace ros2_bridge_sim
{
namespace action
{

struct Drive
{
  struct Goal
  {
    using ros2_message_tag = void;
    static constexpr const char * ros2_message_type = "ros2_bridge_sim/action/Drive_Goal";
    using SharedPtr = std::shared_ptr<Goal>;
    using ConstSharedPtr = std::shared_ptr<const Goal>;
    using UniquePtr = std::unique_ptr<Goal>;
    using _distance_type = double;
    double distance = 0.0;

    template <class Archive>
    void visit(Archive & ar)
    {
      ar(distance);
    }

    template <class Archive>
    void visit(Archive & ar) const
    {
      ar(distance);
    }

    bool operator==(const Goal & other) const
    {
      return distance == other.distance;
    }
    bool operator!=(const Goal & other) const {return !(*this == other);}
  };

  struct Feedback
  {
    using ros2_message_tag = void;
    static constexpr const char * ros2_message_type = "ros2_bridge_sim/action/Drive_Feedback";
    using SharedPtr = std::shared_ptr<Feedback>;
    using ConstSharedPtr = std::shared_ptr<const Feedback>;
    using UniquePtr = std::unique_ptr<Feedback>;
    using _travelled_type = double;
    double travelled = 0.0;
    using _remaining_type = double;
    double remaining = 0.0;

    template <class Archive>
    void visit(Archive & ar)
    {
      ar(travelled);
      ar(remaining);
    }

    template <class Archive>
    void visit(Archive & ar) const
    {
      ar(travelled);
      ar(remaining);
    }

    bool operator==(const Feedback & other) const
    {
      return travelled == other.travelled &&
        remaining == other.remaining;
    }
    bool operator!=(const Feedback & other) const {return !(*this == other);}
  };

  struct Result
  {
    using ros2_message_tag = void;
    static constexpr const char * ros2_message_type = "ros2_bridge_sim/action/Drive_Result";
    using SharedPtr = std::shared_ptr<Result>;
    using ConstSharedPtr = std::shared_ptr<const Result>;
    using UniquePtr = std::unique_ptr<Result>;
    using _travelled_type = double;
    double travelled = 0.0;
    using _success_type = bool;
    bool success = false;

    template <class Archive>
    void visit(Archive & ar)
    {
      ar(travelled);
      ar(success);
    }

    template <class Archive>
    void visit(Archive & ar) const
    {
      ar(travelled);
      ar(success);
    }

    bool operator==(const Result & other) const
    {
      return travelled == other.travelled &&
        success == other.success;
    }
    bool operator!=(const Result & other) const {return !(*this == other);}
  };
};

}  // namespace action
}  // namespace ros2_bridge_sim

#endif  // ROS2_BRIDGE_SIM_ACTION_MESSAGES_HPP
