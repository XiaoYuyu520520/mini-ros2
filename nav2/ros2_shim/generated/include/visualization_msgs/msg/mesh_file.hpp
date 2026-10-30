#ifndef VISUALIZATION_MSGS_MSG_MESHFILE_HPP_
#define VISUALIZATION_MSGS_MSG_MESHFILE_HPP_

#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace visualization_msgs
{
namespace msg
{

struct MeshFile
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "visualization_msgs/msg/MeshFile";
  using SharedPtr = std::shared_ptr<MeshFile>;
  using ConstSharedPtr = std::shared_ptr<const MeshFile>;
  using UniquePtr = std::unique_ptr<MeshFile>;
  using _filename_type = std::string;
  std::string filename;
  using _data_type = std::vector<uint8_t>;
  std::vector<uint8_t> data;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(filename);
    ar(data);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(filename);
    ar(data);
  }

  bool operator==(const MeshFile & other) const
  {
    return filename == other.filename &&
      data == other.data;
  }
  bool operator!=(const MeshFile & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace visualization_msgs

#endif  // VISUALIZATION_MSGS_MSG_MESHFILE_HPP_
