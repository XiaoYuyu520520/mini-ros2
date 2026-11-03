#ifndef RCLCPP__PUBLISHER_HPP_
#define RCLCPP__PUBLISHER_HPP_

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "rclcpp/callback_group.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/qos.hpp"
#include "ros2_shim/graph.hpp"
#include "ros2_shim/serialization.hpp"
#include "shm_transport/shm_channel.hpp"

namespace rclcpp
{

struct PublisherOptions
{
  CallbackGroup::SharedPtr callback_group;
  bool use_intra_process_comm = false;
  QosOverridingOptions qos_overriding_options;
  PublisherEventCallbacks event_callbacks;
};

class PublisherBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(PublisherBase)

  virtual ~PublisherBase() = default;
  virtual const char * get_topic_name() const = 0;
  virtual size_t get_subscription_count() const {return 0;}
  virtual size_t get_intra_process_subscription_count() const {return 0;}
  virtual bool is_activated() const {return true;}
};

template<typename MessageT, typename Allocator = std::allocator<void>>
class Publisher : public PublisherBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(Publisher<MessageT, Allocator>)

  using MessageType = MessageT;

  Publisher(
    std::string topic, std::shared_ptr<shm_transport::ShmChannel> channel,
    const rclcpp::QoS & qos = rclcpp::QoS(10))
  : topic_(std::move(topic)), channel_(std::move(channel)), qos_(qos)
  {
  }

  virtual ~Publisher() = default;

  void publish(const MessageT & message)
  {
    const auto bytes = ros2_shim::serialize(message);
    const bool ok = channel_->publish(bytes.data(), bytes.size());
    if (!ok) {
      notePublishFailed(bytes.size());
    }
    tracePublish(bytes.size(), ok);
  }

  void publish(std::unique_ptr<MessageT> message)
  {
    if (message) {
      publish(*message);
    }
  }

  const char * get_topic_name() const override {return topic_.c_str();}

  // Samples dropped because they did not fit the slot. `publish()` reports
  // success per call; this is the total a caller can log at shutdown.
  uint64_t oversized_count() const {return oversized_;}

  // Counted from the shared graph rather than from DDS: nodes register their
  // subscriptions there and the gRPC bridge registers the topics it mirrors in
  // from the peer machine. The count spans every process, because nav2 uses it
  // to decide whether to publish at all - `ControllerServer::publishVelocity()`
  // writes nothing unless this returns a subscriber, which in this port is
  // normally a different process than the one asking.
  size_t get_subscription_count() const override
  {
    const std::size_t count = ros2_shim::graph::subscription_count(topic_);
    traceCount(count);
    return count;
  }

  const rclcpp::QoS & get_qos() const {return qos_;}

  std::shared_ptr<shm_transport::ShmChannel> get_channel() const {return channel_;}

protected:
  // Opt-in tracing (NAV2_SHM_PUB_DEBUG=<topic>, or `*` for all). Both halves of
  // "the robot does not move" are silent by design: the graph gate drops the
  // message before it is written, and a message written into a segment nobody
  // else is reading looks exactly like one that was never written. Report both
  // - the first sample, then every change, then a heartbeat.
  void tracePublish(std::size_t size, bool ok) const
  {
    if (!traceEnabled()) {
      return;
    }
    ++trace_writes_;
    if (trace_writes_ == 1 || trace_writes_ % 200 == 0) {
      std::fprintf(
        stderr, "[shim_pub] write topic=%s size=%zu ok=%s count=%zu writes=%llu\n",
        topic_.c_str(), size, ok ? "yes" : "no",
        ros2_shim::graph::subscription_count(topic_),
        static_cast<unsigned long long>(trace_writes_));
      std::fflush(stderr);
    }
  }

  void traceCount(std::size_t count) const
  {
    if (!traceEnabled() || count == traceLastCount_) {
      return;
    }
    traceLastCount_ = count;
    std::fprintf(
      stderr, "[shim_pub] gate topic=%s subscribers=%zu -> %s\n", topic_.c_str(), count,
      count > 0 ? "publish" : "DROP (nav2 asks before it writes anything)");
    std::fflush(stderr);
  }

  // A failed publish used to be completely silent: the sample just vanished,
  // and the one case that actually happens in the field - a message larger
  // than the channel's slot - looked on every screen exactly like a topic
  // nobody is publishing. Report the first failure of this publisher at full
  // volume (the sizes and the two environment variables that fix it) and
  // keep counting afterwards; the counter is what a long run summarizes.
  void notePublishFailed(std::size_t size)
  {
    ++oversized_;
    if (oversized_ == 1) {
      std::fprintf(
        stderr,
        "[shim_pub] %s: a %zu byte sample does not fit this channel's slot "
        "(%u bytes) and was dropped. Raise NAV2_SHM_SLOT_SIZE / "
        "NAV2_SHM_IMAGE_SLOT_SIZE / NAV2_SHM_LARGE_SLOT_SIZE (publisher and "
        "subscriber need the same value; restart both ends), and check that "
        "the subscriber on this topic really opened the same channel.\n",
        topic_.c_str(), size, channel_ ? channel_->slot_size() : 0u);
      std::fflush(stderr);
    }
  }

  bool traceEnabled() const
  {
    const char * filter = std::getenv("NAV2_SHM_PUB_DEBUG");
    return filter != nullptr && (std::string(filter) == "*" || topic_ == filter);
  }

  mutable std::size_t traceLastCount_ = ~std::size_t(0);
  mutable std::size_t trace_writes_ = 0;
  mutable uint64_t oversized_ = 0;

  std::string topic_;
  std::shared_ptr<shm_transport::ShmChannel> channel_;
  rclcpp::QoS qos_;
};

}  // namespace rclcpp

#endif  // RCLCPP__PUBLISHER_HPP_
