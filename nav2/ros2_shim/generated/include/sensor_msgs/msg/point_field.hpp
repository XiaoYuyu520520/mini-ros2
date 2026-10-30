#ifndef SENSOR_MSGS_MSG_POINTFIELD_HPP_
#define SENSOR_MSGS_MSG_POINTFIELD_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct PointField
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/PointField";
  using SharedPtr = std::shared_ptr<PointField>;
  using ConstSharedPtr = std::shared_ptr<const PointField>;
  using UniquePtr = std::unique_ptr<PointField>;
  static constexpr uint8_t INT8 = 1;
  static constexpr uint8_t UINT8 = 2;
  static constexpr uint8_t INT16 = 3;
  static constexpr uint8_t UINT16 = 4;
  static constexpr uint8_t INT32 = 5;
  static constexpr uint8_t UINT32 = 6;
  static constexpr uint8_t FLOAT32 = 7;
  static constexpr uint8_t FLOAT64 = 8;
  static constexpr uint8_t INT64 = 9;
  static constexpr uint8_t UINT64 = 10;
  static constexpr uint8_t BOOL = 11;

  using _name_type = std::string;
  std::string name;
  using _offset_type = uint32_t;
  uint32_t offset = 0;
  using _datatype_type = uint8_t;
  uint8_t datatype = 0;
  using _count_type = uint32_t;
  uint32_t count = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(name);
    ar(offset);
    ar(datatype);
    ar(count);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(name);
    ar(offset);
    ar(datatype);
    ar(count);
  }

  bool operator==(const PointField & other) const
  {
    return name == other.name &&
      offset == other.offset &&
      datatype == other.datatype &&
      count == other.count;
  }
  bool operator!=(const PointField & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_POINTFIELD_HPP_
