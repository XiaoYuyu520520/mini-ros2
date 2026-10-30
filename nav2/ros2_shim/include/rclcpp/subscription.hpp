#ifndef RCLCPP__SUBSCRIPTION_HPP_
#define RCLCPP__SUBSCRIPTION_HPP_

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "rclcpp/callback_group.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/qos.hpp"
#include "ros2_shim/serialization.hpp"
#include "shm_transport/shm_channel.hpp"

namespace rclcpp
{

struct SubscriptionOptions
{
  CallbackGroup::SharedPtr callback_group;
  bool use_intra_process_comm = false;
  QosOverridingOptions qos_overriding_options;
  SubscriptionEventCallbacks event_callbacks;
};

class SubscriptionBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(SubscriptionBase)

  virtual ~SubscriptionBase() = default;
  virtual const char * get_topic_name() const = 0;
  virtual void poll() = 0;
  virtual size_t get_publisher_count() const {return 1;}
};

template<typename MessageT, typename Allocator = std::allocator<void>>
class Subscription : public SubscriptionBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(Subscription<MessageT, Allocator>)

  using MessageType = MessageT;

  template<typename CallbackT>
  Subscription(
    std::string topic, std::shared_ptr<shm_transport::ShmChannel> channel,
    const rclcpp::QoS & qos, CallbackT && callback, uint32_t slot_size)
  : topic_(std::move(topic)), channel_(std::move(channel)), qos_(qos), buffer_(slot_size)
  {
    using Callback = std::decay_t<CallbackT>;
    callback_ = [cb = Callback(std::forward<CallbackT>(callback))](
      std::shared_ptr<const MessageT> message) mutable
      {
        if constexpr (std::is_invocable_v<Callback &, std::shared_ptr<const MessageT>>) {
          cb(message);
        } else if constexpr (std::is_invocable_v<Callback &, std::shared_ptr<MessageT>>) {
          cb(std::const_pointer_cast<MessageT>(message));
        } else if constexpr (std::is_invocable_v<Callback &, const MessageT &>) {
          cb(*message);
        } else {
          cb(*message);
        }
      };
  }

  const char * get_topic_name() const override {return topic_.c_str();}

  const rclcpp::QoS & get_qos() const {return qos_;}

  // Skips messages that are already queued in the channel so the callback
  // only sees what is published from now on. Returns the number skipped.
  uint64_t skip_backlog()
  {
    return channel_ ? channel_->seek_to_latest() : 0;
  }

  // The `transient_local` counterpart: the sample the publisher latched is
  // part of the state a late subscriber is entitled to, so the reader starts
  // one behind the head instead of at it.
  uint64_t start_from_latched()
  {
    return channel_ ? channel_->seek_to_latched() : 0;
  }

  // Opt-in tracing (NAV2_SHM_SUB_DEBUG=<topic>, or `*` for all): a channel can
  // be empty, or the reader can be looking at a different segment than the one
  // the publisher writes into, and the two look the same from the callback.
  void traceChannels(const char * phase)
  {
    const char * filter = std::getenv("NAV2_SHM_SUB_DEBUG");
    if (filter == nullptr) {
      return;
    }
    // The first calls of every subscription are always reported: whether a
    // subscription is polled at all is itself one of the things under test.
    const bool filtered = (std::string(filter) != "*" && topic_ != filter);
    if (filtered && trace_calls_ > 3) {
      return;
    }
    const uint64_t published = channel_ ? channel_->published_count() : 0;
    const uint64_t read = channel_ ? channel_->read_count() : 0;
    const uint64_t dropped = channel_ ? channel_->dropped_count() : 0;
    // Only when something moved, plus a heartbeat, so a 40 Hz topic does not
    // bury the topic under investigation.
    if (read != trace_last_read_ || published != trace_last_published_ ||
      ++trace_calls_ % 2000 == 0)
    {
      trace_last_read_ = read;
      trace_last_published_ = published;
      std::fprintf(
        stderr,
        "[shim_sub] %s topic=%s filter=%s channel=%s published=%llu read=%llu dropped=%llu\n",
        phase, topic_.c_str(), filter, channel_ ? channel_->name().c_str() : "(none)",
        static_cast<unsigned long long>(published),
        static_cast<unsigned long long>(read),
        static_cast<unsigned long long>(dropped));
      std::fflush(stderr);
    }
  }

  void poll() override
  {
    traceChannels("poll");
    for (;;) {
      size_t size = 0;
      if (!channel_->subscribe(buffer_.data(), buffer_.size(), &size, 0)) {
        return;
      }
      auto message = std::make_shared<MessageT>();
      if (!ros2_shim::deserialize(buffer_.data(), size, *message)) {
        continue;
      }
      callback_(message);
    }
  }

private:
  uint64_t trace_calls_ = 0;
  uint64_t trace_last_read_ = ~0ull;
  uint64_t trace_last_published_ = ~0ull;
  std::string topic_;
  std::shared_ptr<shm_transport::ShmChannel> channel_;
  rclcpp::QoS qos_;
  std::function<void(std::shared_ptr<const MessageT>)> callback_;
  std::vector<uint8_t> buffer_;
};

}  // namespace rclcpp

#endif  // RCLCPP__SUBSCRIPTION_HPP_
