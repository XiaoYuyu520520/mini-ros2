#ifndef RCLCPP__QOS_HPP_
#define RCLCPP__QOS_HPP_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <vector>

#include "rclcpp/duration.hpp"

typedef enum rmw_qos_history_policy_e
{
  RMW_QOS_POLICY_HISTORY_SYSTEM_DEFAULT,
  RMW_QOS_POLICY_HISTORY_KEEP_LAST,
  RMW_QOS_POLICY_HISTORY_KEEP_ALL,
} rmw_qos_history_policy_t;

typedef enum rmw_qos_reliability_policy_e
{
  RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT,
  RMW_QOS_POLICY_RELIABILITY_RELIABLE,
  RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT,
} rmw_qos_reliability_policy_t;

typedef enum rmw_qos_durability_policy_e
{
  RMW_QOS_POLICY_DURABILITY_SYSTEM_DEFAULT,
  RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL,
  RMW_QOS_POLICY_DURABILITY_VOLATILE,
} rmw_qos_durability_policy_t;

typedef enum rmw_qos_liveliness_policy_e
{
  RMW_QOS_POLICY_LIVELINESS_SYSTEM_DEFAULT,
  RMW_QOS_POLICY_LIVELINESS_AUTOMATIC,
  RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC,
} rmw_qos_liveliness_policy_t;

typedef struct rmw_time_s
{
  uint64_t sec;
  uint64_t nsec;
} rmw_time_t;

typedef struct rmw_qos_profile_s
{
  rmw_qos_history_policy_t history;
  size_t depth;
  rmw_qos_reliability_policy_t reliability;
  rmw_qos_durability_policy_t durability;
  rmw_time_t deadline;
  rmw_time_t lifespan;
  rmw_qos_liveliness_policy_t liveliness;
  rmw_time_t liveliness_lease_duration;
  bool avoid_ros_namespace_conventions;
} rmw_qos_profile_t;

static const rmw_qos_profile_t rmw_qos_profile_system_default = {
  RMW_QOS_POLICY_HISTORY_SYSTEM_DEFAULT, 0,
  RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT, RMW_QOS_POLICY_DURABILITY_SYSTEM_DEFAULT,
  {0, 0}, {0, 0}, RMW_QOS_POLICY_LIVELINESS_SYSTEM_DEFAULT, {0, 0}, false};
static const rmw_qos_profile_t rmw_qos_profile_default = {
  RMW_QOS_POLICY_HISTORY_KEEP_LAST, 10,
  RMW_QOS_POLICY_RELIABILITY_RELIABLE, RMW_QOS_POLICY_DURABILITY_VOLATILE,
  {0, 0}, {0, 0}, RMW_QOS_POLICY_LIVELINESS_AUTOMATIC, {0, 0}, false};
static const rmw_qos_profile_t rmw_qos_profile_sensor_data = {
  RMW_QOS_POLICY_HISTORY_KEEP_LAST, 5,
  RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT, RMW_QOS_POLICY_DURABILITY_VOLATILE,
  {0, 0}, {0, 0}, RMW_QOS_POLICY_LIVELINESS_AUTOMATIC, {0, 0}, false};
static const rmw_qos_profile_t rmw_qos_profile_services_default = {
  RMW_QOS_POLICY_HISTORY_KEEP_LAST, 10,
  RMW_QOS_POLICY_RELIABILITY_RELIABLE, RMW_QOS_POLICY_DURABILITY_VOLATILE,
  {0, 0}, {0, 0}, RMW_QOS_POLICY_LIVELINESS_AUTOMATIC, {0, 0}, false};
static const rmw_qos_profile_t rmw_qos_profile_parameters = {
  RMW_QOS_POLICY_HISTORY_KEEP_LAST, 1000,
  RMW_QOS_POLICY_RELIABILITY_RELIABLE, RMW_QOS_POLICY_DURABILITY_VOLATILE,
  {0, 0}, {0, 0}, RMW_QOS_POLICY_LIVELINESS_AUTOMATIC, {0, 0}, false};

namespace rclcpp
{

enum class HistoryPolicy
{
  SYSTEM_DEFAULT = RMW_QOS_POLICY_HISTORY_SYSTEM_DEFAULT,
  KEEP_LAST = RMW_QOS_POLICY_HISTORY_KEEP_LAST,
  KEEP_ALL = RMW_QOS_POLICY_HISTORY_KEEP_ALL,
};

enum class ReliabilityPolicy
{
  SYSTEM_DEFAULT = RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT,
  RELIABLE = RMW_QOS_POLICY_RELIABILITY_RELIABLE,
  BEST_EFFORT = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT,
};

enum class DurabilityPolicy
{
  SYSTEM_DEFAULT = RMW_QOS_POLICY_DURABILITY_SYSTEM_DEFAULT,
  TRANSIENT_LOCAL = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL,
  VOLATILE = RMW_QOS_POLICY_DURABILITY_VOLATILE,
};

enum class LivelinessPolicy
{
  SYSTEM_DEFAULT = RMW_QOS_POLICY_LIVELINESS_SYSTEM_DEFAULT,
  AUTOMATIC = RMW_QOS_POLICY_LIVELINESS_AUTOMATIC,
  MANUAL_BY_TOPIC = RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC,
};

struct KeepLast
{
  size_t depth;
  explicit KeepLast(size_t depth_) : depth(depth_) {}
};

struct KeepAll
{
};

class QoSInitialization
{
public:
  explicit QoSInitialization(size_t history_depth)
  : history(HistoryPolicy::KEEP_LAST), depth(history_depth)
  {
  }

  HistoryPolicy history;
  size_t depth;
};

class QoS
{
public:
  explicit QoS(size_t history_depth)
  : QoS(QoSInitialization(history_depth), rmw_qos_profile_default)
  {
  }

  explicit QoS(const QoSInitialization & initialization)
  : QoS(initialization, rmw_qos_profile_default)
  {
  }

  QoS(const QoSInitialization & initialization, const rmw_qos_profile_t & profile)
  : profile_(profile)
  {
    profile_.history = static_cast<rmw_qos_history_policy_t>(initialization.history);
    profile_.depth = initialization.depth;
  }

  QoS(const KeepLast & keep_last, const rmw_qos_profile_t & profile = rmw_qos_profile_default)
  : profile_(profile)
  {
    profile_.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
    profile_.depth = keep_last.depth;
  }

  QoS(const KeepAll &, const rmw_qos_profile_t & profile = rmw_qos_profile_default)
  : profile_(profile)
  {
    profile_.history = RMW_QOS_POLICY_HISTORY_KEEP_ALL;
    profile_.depth = 0;
  }

  QoS & keep_last(size_t depth)
  {
    profile_.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
    profile_.depth = depth;
    return *this;
  }

  QoS & keep_all()
  {
    profile_.history = RMW_QOS_POLICY_HISTORY_KEEP_ALL;
    profile_.depth = 0;
    return *this;
  }

  QoS & reliable()
  {
    profile_.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
    return *this;
  }

  QoS & best_effort()
  {
    profile_.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
    return *this;
  }

  QoS & durability_volatile()
  {
    profile_.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
    return *this;
  }

  QoS & transient_local()
  {
    profile_.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
    return *this;
  }

  QoS & deadline(const Duration & deadline)
  {
    profile_.deadline.sec = static_cast<uint64_t>(deadline.nanoseconds() / 1000000000LL);
    profile_.deadline.nsec = static_cast<uint64_t>(deadline.nanoseconds() % 1000000000LL);
    return *this;
  }

  QoS & lifespan(const Duration & lifespan)
  {
    profile_.lifespan.sec = static_cast<uint64_t>(lifespan.nanoseconds() / 1000000000LL);
    profile_.lifespan.nsec = static_cast<uint64_t>(lifespan.nanoseconds() % 1000000000LL);
    return *this;
  }

  QoS & history(HistoryPolicy history)
  {
    profile_.history = static_cast<rmw_qos_history_policy_t>(history);
    return *this;
  }

  QoS & reliability(ReliabilityPolicy reliability)
  {
    profile_.reliability = static_cast<rmw_qos_reliability_policy_t>(reliability);
    return *this;
  }

  QoS & durability(DurabilityPolicy durability)
  {
    profile_.durability = static_cast<rmw_qos_durability_policy_t>(durability);
    return *this;
  }

  const rmw_qos_profile_t & get_rmw_qos_profile() const {return profile_;}

  size_t depth() const {return profile_.depth;}

  HistoryPolicy get_history() const
  {
    return static_cast<HistoryPolicy>(profile_.history);
  }

  ReliabilityPolicy get_reliability() const
  {
    return static_cast<ReliabilityPolicy>(profile_.reliability);
  }

  DurabilityPolicy get_durability() const
  {
    return static_cast<DurabilityPolicy>(profile_.durability);
  }

private:
  rmw_qos_profile_t profile_ = rmw_qos_profile_default;
};

inline QoS SystemDefaultsQoS()
{
  return QoS(QoSInitialization(0), rmw_qos_profile_system_default);
}

inline QoS SensorDataQoS()
{
  return QoS(QoSInitialization(5), rmw_qos_profile_sensor_data);
}

inline QoS ServicesQoS()
{
  return QoS(QoSInitialization(10), rmw_qos_profile_services_default);
}

inline QoS ParametersQoS()
{
  return QoS(QoSInitialization(1000), rmw_qos_profile_parameters);
}

// Upstream `rclcpp::RosoutQoS()`: keep last 1000, reliable, volatile - which is
// the profile `rmw_qos_profile_parameters` already carries, named here for the
// topic it is used on so a reader of `create_publisher("/rosout", ...)` sees
// what upstream has there.
inline QoS RosoutQoS()
{
  return QoS(QoSInitialization(1000), rmw_qos_profile_parameters);
}

enum class QosPolicyKind
{
  AvoidRosNamespaceConventions,
  Deadline,
  Depth,
  Durability,
  History,
  Lifespan,
  Liveliness,
  LivelinessLeaseDuration,
  Reliability,
};

class QosOverridingOptions
{
public:
  QosOverridingOptions() = default;

  explicit QosOverridingOptions(const std::vector<QosPolicyKind> & policy_kinds)
  : policy_kinds_(policy_kinds)
  {
  }

  QosOverridingOptions(std::initializer_list<QosPolicyKind> policy_kinds)
  : policy_kinds_(policy_kinds)
  {
  }

  const std::vector<QosPolicyKind> & policy_kinds() const {return policy_kinds_;}

private:
  std::vector<QosPolicyKind> policy_kinds_;
};

struct QOSMessageLostInfo
{
  uint64_t total_count = 0;
  uint64_t total_count_change = 0;
};

struct MatchedInfo
{
  int current_count = 0;
  int current_count_change = 0;
};

using QOSMessageLostCallbackType = std::function<void(QOSMessageLostInfo &)>;
using SubscriptionMatchedCallbackType = std::function<void(MatchedInfo &)>;
using PublisherMatchedCallbackType = std::function<void(MatchedInfo &)>;
using IncompatibleTypeCallbackType = std::function<void(void)>;
using QOSRequestedIncompatibleQoSCallbackType = std::function<void(void)>;
using QOSOfferedIncompatibleQoSCallbackType = std::function<void(void)>;
using QOSDeadlineRequestedCallbackType = std::function<void(void)>;
using QOSDeadlineOfferedCallbackType = std::function<void(void)>;
using QOSLivelinessChangedCallbackType = std::function<void(void)>;
using QOSLivelinessLostCallbackType = std::function<void(void)>;

struct SubscriptionEventCallbacks
{
  QOSMessageLostCallbackType message_lost_callback;
  SubscriptionMatchedCallbackType matched_callback;
  IncompatibleTypeCallbackType incompatible_type_callback;
  QOSRequestedIncompatibleQoSCallbackType incompatible_qos_callback;
  QOSDeadlineRequestedCallbackType deadline_callback;
  QOSLivelinessChangedCallbackType liveliness_callback;
};

struct PublisherEventCallbacks
{
  PublisherMatchedCallbackType matched_callback;
  IncompatibleTypeCallbackType incompatible_type_callback;
  QOSOfferedIncompatibleQoSCallbackType incompatible_qos_callback;
  QOSDeadlineOfferedCallbackType deadline_callback;
  QOSLivelinessLostCallbackType liveliness_callback;
};

}  // namespace rclcpp

#endif  // RCLCPP__QOS_HPP_
