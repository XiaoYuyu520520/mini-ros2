#ifndef STATISTICS_MSGS_MSG_METRICSMESSAGE_HPP_
#define STATISTICS_MSGS_MSG_METRICSMESSAGE_HPP_

#include "builtin_interfaces/msg/time.hpp"
#include "statistics_msgs/msg/statistic_data_point.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace statistics_msgs
{
namespace msg
{

struct MetricsMessage
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "statistics_msgs/msg/MetricsMessage";
  using SharedPtr = std::shared_ptr<MetricsMessage>;
  using ConstSharedPtr = std::shared_ptr<const MetricsMessage>;
  using UniquePtr = std::unique_ptr<MetricsMessage>;
  using _measurement_source_name_type = std::string;
  std::string measurement_source_name;
  using _metrics_source_type = std::string;
  std::string metrics_source;
  using _unit_type = std::string;
  std::string unit;
  using _window_start_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time window_start;
  using _window_stop_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time window_stop;
  using _statistics_type = std::vector<::statistics_msgs::msg::StatisticDataPoint>;
  std::vector<::statistics_msgs::msg::StatisticDataPoint> statistics;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(measurement_source_name);
    ar(metrics_source);
    ar(unit);
    ar(window_start);
    ar(window_stop);
    ar(statistics);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(measurement_source_name);
    ar(metrics_source);
    ar(unit);
    ar(window_start);
    ar(window_stop);
    ar(statistics);
  }

  bool operator==(const MetricsMessage & other) const
  {
    return measurement_source_name == other.measurement_source_name &&
      metrics_source == other.metrics_source &&
      unit == other.unit &&
      window_start == other.window_start &&
      window_stop == other.window_stop &&
      statistics == other.statistics;
  }
  bool operator!=(const MetricsMessage & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace statistics_msgs

#endif  // STATISTICS_MSGS_MSG_METRICSMESSAGE_HPP_
