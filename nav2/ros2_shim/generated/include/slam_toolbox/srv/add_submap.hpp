#ifndef SLAM_TOOLBOX_SRV_ADDSUBMAP_HPP_
#define SLAM_TOOLBOX_SRV_ADDSUBMAP_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace slam_toolbox
{
namespace srv
{

struct AddSubmap
{
  static constexpr const char * ros2_service_type = "slam_toolbox/srv/AddSubmap";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "slam_toolbox/srv/AddSubmap_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _filename_type = std::string;
      std::string filename;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(filename);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(filename);
      }

      bool operator==(const Request & other) const
      {
        return filename == other.filename;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "slam_toolbox/srv/AddSubmap_Response";
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

#endif  // SLAM_TOOLBOX_SRV_ADDSUBMAP_HPP_
