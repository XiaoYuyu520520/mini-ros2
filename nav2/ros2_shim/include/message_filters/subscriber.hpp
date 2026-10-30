#ifndef MESSAGE_FILTERS__SUBSCRIBER_HPP_
#define MESSAGE_FILTERS__SUBSCRIBER_HPP_

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "rclcpp/rclcpp.hpp"

namespace message_filters
{

/**
 * Handle returned by Subscriber::registerCallback()/connectInput().
 *
 * Unlike ROS1, the destructor does *not* disconnect: several call sites in the
 * ported nav2 code (tf2_ros::MessageFilter::connect() among them) discard the
 * returned handle on purpose, and an implicit disconnect would silently drop
 * their callbacks. Call disconnect() explicitly, the way AmclNode does.
 */
class Connection
{
public:
  Connection() = default;
  explicit Connection(std::function<void()> on_disconnect)
  : on_disconnect_(std::move(on_disconnect)) {}

  void disconnect()
  {
    if (on_disconnect_) {
      auto callback = std::move(on_disconnect_);
      on_disconnect_ = nullptr;
      callback();
    }
    connected_ = false;
  }

  bool isConnected() const {return connected_ && static_cast<bool>(on_disconnect_);}

  void setDisconnectCallback(std::function<void()> on_disconnect)
  {
    on_disconnect_ = std::move(on_disconnect);
  }

private:
  std::function<void()> on_disconnect_;
  bool connected_ = true;
};

/**
 * The node-type-erased face of a Subscriber.
 *
 * `nav2_costmap_2d::ObstacleLayer` keeps one vector of these so the laser
 * subscribers and the point-cloud subscribers can sit side by side, and it
 * drives them through it: `activate()` re-subscribes, `deactivate()`
 * unsubscribes. `subscribe()` with no arguments is that re-subscribe - the
 * node and topic the subscriber was built with are remembered for it.
 */
template<class NodeType>
class SubscriberBase
{
public:
  using SharedPtr = std::shared_ptr<SubscriberBase<NodeType>>;
  using WeakPtr = std::weak_ptr<SubscriberBase<NodeType>>;

  virtual ~SubscriberBase() = default;

  virtual void subscribe() = 0;
  virtual void unsubscribe() = 0;
  virtual std::string getTopic() const = 0;
  virtual bool isSubscribed() const = 0;
};

template<class MessageT, class NodeT = void>
class Subscriber : public SubscriberBase<NodeT>
{
public:
  using SharedPtr = std::shared_ptr<Subscriber<MessageT, NodeT>>;
  using MessageType = MessageT;
  using Callback = std::function<void(const std::shared_ptr<const MessageT> &)>;

  template<class NodePtr>
  Subscriber(
    NodePtr node, const std::string & topic, const rmw_qos_profile_t & qos,
    const rclcpp::SubscriptionOptions & options = rclcpp::SubscriptionOptions())
  {
    subscribe(node, topic, fromProfile(qos), options);
  }

  template<class NodePtr>
  Subscriber(
    NodePtr node, const std::string & topic, const rclcpp::QoS & qos,
    const rclcpp::SubscriptionOptions & options = rclcpp::SubscriptionOptions())
  {
    subscribe(node, topic, qos, options);
  }

  template<class NodePtr>
  Subscriber(NodePtr node, const std::string & topic)
  {
    subscribe(node, topic, rclcpp::QoS(10), rclcpp::SubscriptionOptions());
  }

  template<class NodePtr>
  void subscribe(
    NodePtr node, const std::string & topic, const rclcpp::QoS & qos,
    const rclcpp::SubscriptionOptions & options = rclcpp::SubscriptionOptions())
  {
    (void)options;
    topic_ = topic;
    qos_ = qos;
    subscription_ = node->template create_subscription<MessageT>(
      topic, qos, [this](const std::shared_ptr<const MessageT> message) {
        if (callback_) {
          callback_(message);
        }
      });
    remember(node, topic, qos);
  }

  void subscribe() override
  {
    if (resubscribe_) {
      resubscribe_();
    }
  }

  void unsubscribe() override
  {
    subscription_.reset();
  }

  std::string getTopic() const override {return topic_;}

  bool isSubscribed() const override {return static_cast<bool>(subscription_);}

  Connection registerCallback(Callback callback)
  {
    callback_ = std::move(callback);
    Connection connection;
    connection.setDisconnectCallback([this]() {
        callback_ = nullptr;
        subscription_.reset();
      });
    return connection;
  }

  rclcpp::Subscription<MessageT>::SharedPtr getSubscriber() const {return subscription_;}

private:
  /// The rmw profile carries the history depth, the rest of the QoS policy is
  /// copied across as it stands; that is the whole of what this shim reads off
  /// a QoS (see rclcpp/subscription.hpp).
  static rclcpp::QoS fromProfile(const rmw_qos_profile_t & profile)
  {
    return rclcpp::QoS(
      rclcpp::QoSInitialization(profile.depth > 0 ? profile.depth : 1), profile);
  }

  /// Re-subscribing needs the node again. A subscriber built from a
  /// `shared_ptr` holds a weak one, so remembering how to subscribe does not
  /// keep the node alive; one built from a bare pointer (slam_toolbox does
  /// that) has nothing to weaken, and holds the pointer.
  template<class NodePtr>
  void remember(
    const std::shared_ptr<NodePtr> & node, const std::string & topic, const rclcpp::QoS & qos)
  {
    std::weak_ptr<NodePtr> weak = node;
    resubscribe_ = [this, weak, topic, qos]() {
        if (auto node = weak.lock()) {
          subscribe(node, topic, qos);
        }
      };
  }

  template<class NodePtr>
  void remember(NodePtr * node, const std::string & topic, const rclcpp::QoS & qos)
  {
    resubscribe_ = [this, node, topic, qos]() {subscribe(node, topic, qos);};
  }

  typename rclcpp::Subscription<MessageT>::SharedPtr subscription_;
  Callback callback_;
  std::string topic_;
  rclcpp::QoS qos_{10};
  std::function<void()> resubscribe_;
};

}  // namespace message_filters

#endif  // MESSAGE_FILTERS__SUBSCRIBER_HPP_
