#ifndef POINT_CLOUD_TRANSPORT__SUBSCRIBER_FILTER_HPP_
#define POINT_CLOUD_TRANSPORT__SUBSCRIBER_FILTER_HPP_

#include <memory>
#include <string>
#include <utility>

#include "message_filters/subscriber.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"

namespace point_cloud_transport
{

/// A `message_filters::Subscriber<sensor_msgs::msg::PointCloud2>` that also
/// names the transport it subscribes through.
///
/// Upstream's `SubscriberFilter` sits on its plugin-loaded transport layer
/// (`raw`, `compressed`, ...) and hands the points on through it. There is no
/// such layer here - the shared-memory graph carries one representation, the
/// same bytes `raw` would - so the transport name is recorded and reported
/// back for parity, and the subscription itself is the plain one.
///
/// nav2_costmap_2d only reaches for this class on rclcpp >= 30 (see the
/// `RCLCPP_VERSION_GTE` ladder in obstacle_layer.cpp); the shim reports the
/// Jazzy version, so the `message_filters::Subscriber` below is what the
/// ported layers actually construct today.
class SubscriberFilter : public message_filters::Subscriber<sensor_msgs::msg::PointCloud2>
{
public:
  using Base = message_filters::Subscriber<sensor_msgs::msg::PointCloud2>;
  using SharedPtr = std::shared_ptr<SubscriberFilter>;
  using ConstSharedPtr = std::shared_ptr<const SubscriberFilter>;

  SubscriberFilter() = default;

  template<class NodeT>
  SubscriberFilter(
    NodeT && node, const std::string & topic, const std::string & transport_type,
    const rclcpp::QoS & qos,
    const rclcpp::SubscriptionOptions & options = rclcpp::SubscriptionOptions())
  : Base(std::forward<NodeT>(node), topic, qos.get_rmw_qos_profile(), options),
    topic_(topic),
    transport_(transport_type.empty() ? kRawTransport : transport_type)
  {
  }

  template<class NodeT>
  SubscriberFilter(NodeT && node, const std::string & topic)
  : Base(std::forward<NodeT>(node), topic),
    topic_(topic)
  {
  }

  std::string getTopic() const override {return topic_;}
  const std::string & getTransport() const {return transport_;}

  void shutdown() {this->unsubscribe();}

  static constexpr const char * kRawTransport = "raw";

private:
  std::string topic_;
  std::string transport_ = kRawTransport;
};

}  // namespace point_cloud_transport

#endif  // POINT_CLOUD_TRANSPORT__SUBSCRIBER_FILTER_HPP_
