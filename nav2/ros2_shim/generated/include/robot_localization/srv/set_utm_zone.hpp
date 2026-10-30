#ifndef ROBOT_LOCALIZATION_SRV_SETUTMZONE_HPP_
#define ROBOT_LOCALIZATION_SRV_SETUTMZONE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace robot_localization
{
namespace srv
{

struct SetUTMZone
{
  static constexpr const char * ros2_service_type = "robot_localization/srv/SetUTMZone";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "robot_localization/srv/SetUTMZone_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _utm_zone_type = std::string;
      std::string utm_zone;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(utm_zone);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(utm_zone);
      }

      bool operator==(const Request & other) const
      {
        return utm_zone == other.utm_zone;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "robot_localization/srv/SetUTMZone_Response";
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
}  // namespace robot_localization

#endif  // ROBOT_LOCALIZATION_SRV_SETUTMZONE_HPP_
