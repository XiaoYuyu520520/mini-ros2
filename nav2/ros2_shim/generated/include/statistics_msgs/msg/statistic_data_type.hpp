#ifndef STATISTICS_MSGS_MSG_STATISTICDATATYPE_HPP_
#define STATISTICS_MSGS_MSG_STATISTICDATATYPE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace statistics_msgs
{
namespace msg
{

struct StatisticDataType
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "statistics_msgs/msg/StatisticDataType";
  using SharedPtr = std::shared_ptr<StatisticDataType>;
  using ConstSharedPtr = std::shared_ptr<const StatisticDataType>;
  using UniquePtr = std::unique_ptr<StatisticDataType>;
  static constexpr uint8_t STATISTICS_DATA_TYPE_UNINITIALIZED = 0;
  static constexpr uint8_t STATISTICS_DATA_TYPE_AVERAGE = 1;
  static constexpr uint8_t STATISTICS_DATA_TYPE_MINIMUM = 2;
  static constexpr uint8_t STATISTICS_DATA_TYPE_MAXIMUM = 3;
  static constexpr uint8_t STATISTICS_DATA_TYPE_STDDEV = 4;
  static constexpr uint8_t STATISTICS_DATA_TYPE_SAMPLE_COUNT = 5;

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

  bool operator==(const StatisticDataType & other) const
  {
    (void)other;
    return true;
  }
  bool operator!=(const StatisticDataType & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace statistics_msgs

#endif  // STATISTICS_MSGS_MSG_STATISTICDATATYPE_HPP_
