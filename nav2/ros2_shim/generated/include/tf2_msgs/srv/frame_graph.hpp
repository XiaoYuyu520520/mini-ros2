#ifndef TF2_MSGS_SRV_FRAMEGRAPH_HPP_
#define TF2_MSGS_SRV_FRAMEGRAPH_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace tf2_msgs
{
namespace srv
{

struct FrameGraph
{
  static constexpr const char * ros2_service_type = "tf2_msgs/srv/FrameGraph";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "tf2_msgs/srv/FrameGraph_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;

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

      bool operator==(const Request & other) const
      {
        (void)other;
        return true;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "tf2_msgs/srv/FrameGraph_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _frame_yaml_type = std::string;
      std::string frame_yaml;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(frame_yaml);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(frame_yaml);
      }

      bool operator==(const Response & other) const
      {
        return frame_yaml == other.frame_yaml;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace tf2_msgs

#endif  // TF2_MSGS_SRV_FRAMEGRAPH_HPP_
