#ifndef RCLCPP_LIFECYCLE__LIFECYCLE_PUBLISHER_HPP_
#define RCLCPP_LIFECYCLE__LIFECYCLE_PUBLISHER_HPP_

#include <memory>
#include <string>

#include "rclcpp/publisher.hpp"

namespace rclcpp_lifecycle
{

class LifecycleManagedEntity
{
public:
  virtual ~LifecycleManagedEntity() = default;
  virtual void on_activate() {}
  virtual void on_deactivate() {}
  virtual bool is_activated() const {return true;}
};

template<typename MessageT, typename Allocator = std::allocator<void>>
class LifecyclePublisher
  : public rclcpp::Publisher<MessageT, Allocator>, public LifecycleManagedEntity
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(LifecyclePublisher<MessageT, Allocator>)

  using MessageType = MessageT;

  LifecyclePublisher(
    std::string topic, std::shared_ptr<shm_transport::ShmChannel> channel,
    const rclcpp::QoS & qos = rclcpp::QoS(10))
  : rclcpp::Publisher<MessageT, Allocator>(std::move(topic), std::move(channel), qos)
  {
  }

  void on_activate() override {active_ = true;}

  void on_deactivate() override {active_ = false;}

  bool is_activated() const override {return active_;}

  bool is_active() const {return active_;}

  void publish(const MessageT & message)
  {
    if (active_) {
      rclcpp::Publisher<MessageT, Allocator>::publish(message);
    }
  }

  void publish(std::unique_ptr<MessageT> message)
  {
    if (active_ && message) {
      rclcpp::Publisher<MessageT, Allocator>::publish(*message);
    }
  }

private:
  bool active_ = false;
};

}  // namespace rclcpp_lifecycle

#endif  // RCLCPP_LIFECYCLE__LIFECYCLE_PUBLISHER_HPP_
