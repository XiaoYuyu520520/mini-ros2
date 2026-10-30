#ifndef ROBOT_LOCALIZATION_SRV_TOGGLEFILTERPROCESSING_HPP_
#define ROBOT_LOCALIZATION_SRV_TOGGLEFILTERPROCESSING_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace robot_localization
{
namespace srv
{

struct ToggleFilterProcessing
{
  static constexpr const char * ros2_service_type = "robot_localization/srv/ToggleFilterProcessing";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "robot_localization/srv/ToggleFilterProcessing_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _on_type = bool;
      bool on = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(on);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(on);
      }

      bool operator==(const Request & other) const
      {
        return on == other.on;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "robot_localization/srv/ToggleFilterProcessing_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _status_type = bool;
      bool status = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(status);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(status);
      }

      bool operator==(const Response & other) const
      {
        return status == other.status;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace robot_localization

#endif  // ROBOT_LOCALIZATION_SRV_TOGGLEFILTERPROCESSING_HPP_
