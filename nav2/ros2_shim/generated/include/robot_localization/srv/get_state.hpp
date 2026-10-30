#ifndef ROBOT_LOCALIZATION_SRV_GETSTATE_HPP_
#define ROBOT_LOCALIZATION_SRV_GETSTATE_HPP_

#include "array"
#include "builtin_interfaces/msg/time.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace robot_localization
{
namespace srv
{

struct GetState
{
  static constexpr const char * ros2_service_type = "robot_localization/srv/GetState";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "robot_localization/srv/GetState_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _time_stamp_type = ::builtin_interfaces::msg::Time;
      ::builtin_interfaces::msg::Time time_stamp;
      using _frame_id_type = std::string;
      std::string frame_id;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(time_stamp);
        ar(frame_id);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(time_stamp);
        ar(frame_id);
      }

      bool operator==(const Request & other) const
      {
        return time_stamp == other.time_stamp &&
          frame_id == other.frame_id;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "robot_localization/srv/GetState_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _state_type = std::array<double, 15>;
      std::array<double, 15> state = {};
      using _covariance_type = std::array<double, 225>;
      std::array<double, 225> covariance = {};

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(state);
        ar(covariance);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(state);
        ar(covariance);
      }

      bool operator==(const Response & other) const
      {
        return state == other.state &&
          covariance == other.covariance;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace robot_localization

#endif  // ROBOT_LOCALIZATION_SRV_GETSTATE_HPP_
