#ifndef NAV2_MSGS_SRV_ISPATHVALID_HPP_
#define NAV2_MSGS_SRV_ISPATHVALID_HPP_

#include "nav_msgs/msg/path.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace srv
{

struct IsPathValid
{
  static constexpr const char * ros2_service_type = "nav2_msgs/srv/IsPathValid";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/IsPathValid_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _path_type = ::nav_msgs::msg::Path;
      ::nav_msgs::msg::Path path;
      using _max_cost_type = uint8_t;
      uint8_t max_cost = 0;
      using _consider_unknown_as_obstacle_type = bool;
      bool consider_unknown_as_obstacle = false;
      using _layer_name_type = std::string;
      std::string layer_name;
      using _footprint_type = std::string;
      std::string footprint;
      using _stop_at_first_collision_type = bool;
      bool stop_at_first_collision = false;
      using _max_lookahead_distance_type = double;
      double max_lookahead_distance = 0.0;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(path);
        ar(max_cost);
        ar(consider_unknown_as_obstacle);
        ar(layer_name);
        ar(footprint);
        ar(stop_at_first_collision);
        ar(max_lookahead_distance);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(path);
        ar(max_cost);
        ar(consider_unknown_as_obstacle);
        ar(layer_name);
        ar(footprint);
        ar(stop_at_first_collision);
        ar(max_lookahead_distance);
      }

      bool operator==(const Request & other) const
      {
        return path == other.path &&
          max_cost == other.max_cost &&
          consider_unknown_as_obstacle == other.consider_unknown_as_obstacle &&
          layer_name == other.layer_name &&
          footprint == other.footprint &&
          stop_at_first_collision == other.stop_at_first_collision &&
          max_lookahead_distance == other.max_lookahead_distance;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/IsPathValid_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _success_type = bool;
      bool success = false;
      using _is_valid_type = bool;
      bool is_valid = false;
      using _invalid_pose_indices_type = std::vector<int32_t>;
      std::vector<int32_t> invalid_pose_indices;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(success);
        ar(is_valid);
        ar(invalid_pose_indices);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(success);
        ar(is_valid);
        ar(invalid_pose_indices);
      }

      bool operator==(const Response & other) const
      {
        return success == other.success &&
          is_valid == other.is_valid &&
          invalid_pose_indices == other.invalid_pose_indices;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_SRV_ISPATHVALID_HPP_
