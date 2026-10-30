#ifndef NAV2_MSGS_SRV_SAVEMAP_HPP_
#define NAV2_MSGS_SRV_SAVEMAP_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace srv
{

struct SaveMap
{
  static constexpr const char * ros2_service_type = "nav2_msgs/srv/SaveMap";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/SaveMap_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _map_topic_type = std::string;
      std::string map_topic;
      using _map_url_type = std::string;
      std::string map_url;
      using _image_format_type = std::string;
      std::string image_format;
      using _map_mode_type = std::string;
      std::string map_mode;
      using _free_thresh_type = float;
      float free_thresh = 0.0f;
      using _occupied_thresh_type = float;
      float occupied_thresh = 0.0f;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(map_topic);
        ar(map_url);
        ar(image_format);
        ar(map_mode);
        ar(free_thresh);
        ar(occupied_thresh);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(map_topic);
        ar(map_url);
        ar(image_format);
        ar(map_mode);
        ar(free_thresh);
        ar(occupied_thresh);
      }

      bool operator==(const Request & other) const
      {
        return map_topic == other.map_topic &&
          map_url == other.map_url &&
          image_format == other.image_format &&
          map_mode == other.map_mode &&
          free_thresh == other.free_thresh &&
          occupied_thresh == other.occupied_thresh;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/srv/SaveMap_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _result_type = bool;
      bool result = false;

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
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_SRV_SAVEMAP_HPP_
