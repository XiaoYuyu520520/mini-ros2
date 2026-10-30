#ifndef POINT_CLOUD_TRANSPORT__POINT_CLOUD_TRANSPORT_HPP_
#define POINT_CLOUD_TRANSPORT__POINT_CLOUD_TRANSPORT_HPP_

#include <memory>
#include <string>
#include <utility>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"

/// The point-cloud transport layer, as far as the ported code reaches into it.
///
/// Upstream, this is a plugin-loaded family of encodings for
/// `sensor_msgs/msg/PointCloud2`: a publisher and a subscriber agree on a
/// transport name (`raw`, `draco`, `compressed`, ...) and the plugin does the
/// coding. There is no such layer here. The shared-memory graph carries one
/// representation per topic - the same bytes `raw` would put on the wire - and
/// nothing else, so a transport name can only mean `raw`.
///
/// nav2_collision_monitor's point-cloud source is the one ported call site. It
/// reaches for this class only on rclcpp >= 30 (see the `RCLCPP_VERSION_GTE`
/// ladder in pointcloud.cpp); the shim reports the Jazzy version number (28.0),
/// so what the port actually builds is the plain `create_subscription` below.
/// The types exist so that the source compiles either way, and so that a name
/// asked for in a parameters file is checked rather than ignored.
///
/// A transport name other than `raw` is refused at subscribe time instead of
/// being silently downgraded: a parameters file that asks for `compressed` is
/// asking for something this port cannot deliver, and the honest answer is an
/// exception rather than a subscription that looks like it worked.

namespace point_cloud_transport
{

/// The transport a subscription asks for, by name. Upstream also carries the
/// publisher's node options through here (for the plugin loader); there is no
/// loader to configure, so the name is the whole of it.
class TransportHints
{
public:
  explicit TransportHints(const std::string & transport = "")
  : transport_(transport)
  {
  }

  const std::string & getTransport() const {return transport_;}

private:
  std::string transport_;
};

/// The handle a `PointCloudTransport::subscribe` hands back. Upstream's is a
/// thin wrapper over the selected transport's subscriber; this one owns the
/// plain shim subscription and remembers the topic and transport it was built
/// with, the way the caller's `shutdown()` expects.
class Subscriber
{
public:
  using SharedPtr = std::shared_ptr<Subscriber>;

  static constexpr const char * kRawTransport = "raw";

  Subscriber() = default;

  Subscriber(
    rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr subscription,
    std::string topic, std::string transport)
  : subscription_(std::move(subscription)),
    topic_(std::move(topic)),
    transport_(std::move(transport))
  {
  }

  void shutdown() {subscription_.reset();}

  const std::string & getTopic() const {return topic_;}
  const std::string & getTransport() const {return transport_;}
  bool isSubscribed() const {return static_cast<bool>(subscription_);}

private:
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr subscription_;
  std::string topic_;
  std::string transport_ = kRawTransport;
};

/// Creates subscribers for point clouds on a node.
class PointCloudTransport
{
public:
  template<class NodeT>
  explicit PointCloudTransport(NodeT && node, const std::string & = "")
  : node_(&node)
  {
  }

  template<class CallbackT>
  Subscriber subscribe(
    const std::string & topic, const rclcpp::QoS & qos, CallbackT && callback,
    const rclcpp::SubscriptionOptions & = rclcpp::SubscriptionOptions(),
    const TransportHints * hints = nullptr)
  {
    const std::string transport = hints == nullptr ? std::string{} : hints->getTransport();
    if (!transport.empty() && transport != Subscriber::kRawTransport) {
      throw std::runtime_error(
              "point_cloud_transport: transport '" + transport +
              "' is not available in this port; only '" + Subscriber::kRawTransport + "' is");
    }
    auto subscription = node_->create_subscription<sensor_msgs::msg::PointCloud2>(
      topic, qos, std::forward<CallbackT>(callback));
    return Subscriber(
      subscription, topic, transport.empty() ? Subscriber::kRawTransport : transport);
  }

private:
  rclcpp::Node * node_;
};

}  // namespace point_cloud_transport

#endif  // POINT_CLOUD_TRANSPORT__POINT_CLOUD_TRANSPORT_HPP_
