#ifndef SLAM_TOOLBOX_SRV_DESERIALIZEPOSEGRAPH_HPP_
#define SLAM_TOOLBOX_SRV_DESERIALIZEPOSEGRAPH_HPP_

#include "geometry_msgs/msg/pose2_d.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace slam_toolbox
{
namespace srv
{

struct DeserializePoseGraph
{
  static constexpr const char * ros2_service_type = "slam_toolbox/srv/DeserializePoseGraph";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "slam_toolbox/srv/DeserializePoseGraph_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      static constexpr int8_t UNSET = 0;
      static constexpr int8_t START_AT_FIRST_NODE = 1;
      static constexpr int8_t START_AT_GIVEN_POSE = 2;
      static constexpr int8_t LOCALIZE_AT_POSE = 3;

      using _filename_type = std::string;
      std::string filename;
      using _match_type_type = int8_t;
      int8_t match_type = 0;
      using _initial_pose_type = ::geometry_msgs::msg::Pose2D;
      ::geometry_msgs::msg::Pose2D initial_pose;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(filename);
        ar(match_type);
        ar(initial_pose);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(filename);
        ar(match_type);
        ar(initial_pose);
      }

      bool operator==(const Request & other) const
      {
        return filename == other.filename &&
          match_type == other.match_type &&
          initial_pose == other.initial_pose;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "slam_toolbox/srv/DeserializePoseGraph_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;

      template <class Archive>
      void visit(Archive & ar)
      {
        (void)ar;
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        (void)ar;
      }

      bool operator==(const Response & other) const
      {
        (void)other;
        return true;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace slam_toolbox

#endif  // SLAM_TOOLBOX_SRV_DESERIALIZEPOSEGRAPH_HPP_
