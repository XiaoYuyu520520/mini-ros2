#ifndef STATISTICS_MSGS_MSG_STATISTICDATAPOINT_HPP_
#define STATISTICS_MSGS_MSG_STATISTICDATAPOINT_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace statistics_msgs
{
namespace msg
{

struct StatisticDataPoint
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "statistics_msgs/msg/StatisticDataPoint";
  using SharedPtr = std::shared_ptr<StatisticDataPoint>;
  using ConstSharedPtr = std::shared_ptr<const StatisticDataPoint>;
  using UniquePtr = std::unique_ptr<StatisticDataPoint>;
  using _data_type_type = uint8_t;
  uint8_t data_type = 0;
  using _data_type = double;
  double data = 0.0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(data_type);
    ar(data);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(data_type);
    ar(data);
  }

  bool operator==(const StatisticDataPoint & other) const
  {
    return data_type == other.data_type &&
      data == other.data;
  }
  bool operator!=(const StatisticDataPoint & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace statistics_msgs

#endif  // STATISTICS_MSGS_MSG_STATISTICDATAPOINT_HPP_
