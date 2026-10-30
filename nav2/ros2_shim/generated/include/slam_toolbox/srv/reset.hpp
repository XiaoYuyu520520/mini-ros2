#ifndef SLAM_TOOLBOX_SRV_RESET_HPP_
#define SLAM_TOOLBOX_SRV_RESET_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace slam_toolbox
{
namespace srv
{

struct Reset
{
  static constexpr const char * ros2_service_type = "slam_toolbox/srv/Reset";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "slam_toolbox/srv/Reset_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _pause_new_measurements_type = bool;
      bool pause_new_measurements = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(pause_new_measurements);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(pause_new_measurements);
      }

      bool operator==(const Request & other) const
      {
        return pause_new_measurements == other.pause_new_measurements;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "slam_toolbox/srv/Reset_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      static constexpr uint8_t RESULT_SUCCESS = 0;

      using _result_type = uint8_t;
      uint8_t result = 0;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(result);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(result);
      }

      bool operator==(const Response & other) const
      {
        return result == other.result;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace slam_toolbox

#endif  // SLAM_TOOLBOX_SRV_RESET_HPP_
