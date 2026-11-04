#include "ros2_bridge/uplink_forwarder.hpp"

#include <cstdio>

#include "ros2_bridge/shm_topic.hpp"
#include "ros2_bridge/type_registry.hpp"
#include "ros2_shim/graph.hpp"

namespace ros2_bridge
{

UplinkForwarderService::UplinkForwarderService(UplinkContext context)
: context_(std::move(context))
{
}

void UplinkForwarderService::sync()
{
  if (context_.topics == nullptr || context_.topics->empty()) {
    return;
  }
  // Learn the type of every local topic from the shim graph registry; the
  // publisher may live in another process (nav2, ros2 topic pub, ...).
  std::map<std::string, std::string> local_types;
  for (const auto & entry : ros2_shim::graph::list_topics()) {
    if (entry.source == "local" && !entry.type.empty()) {
      local_types[entry.name] = entry.type;
    }
  }

  for (const auto & topic : *context_.topics) {
    // A local publisher is the common case, but `/cmd_vel` is normally
    // created by nav2 *after* the bridge starts, and the agent already knows
    // the type from its own graph - so fall back to what the peer reports.
    std::string type;
    const auto it = local_types.find(topic);
    if (it != local_types.end()) {
      type = it->second;
    } else {
      std::string remote_type;
      if (context_.remote_type_of && context_.remote_type_of(topic, remote_type)) {
        type = remote_type;
      } else {
        ros2_grpc_sdk::TopicInfo info;
        if (context_.client->getTopicInfo(topic, info).ok()) {
          type = info.type;
        }
      }
    }
    if (type.empty()) {
      // Silent until now, and a silent skip is the worst kind: the uplink is
      // then simply never there, `/cmd_vel` published on this machine goes
      // nowhere, and nothing anywhere says so. Say it once per topic.
      bool already_warned = false;
      if (context_.warned_types) {
        std::lock_guard<std::mutex> lock(mutex_);
        already_warned = context_.warned_types->count("uplink-no-type:" + topic) > 0;
        if (!already_warned) {
          context_.warned_types->insert("uplink-no-type:" + topic);
        }
      }
      if (!already_warned) {
        std::fprintf(
          stderr,
          "[bridge] uplink '%s' skipped: neither the local graph nor the peer knows its type\n",
          topic.c_str());
      }
      continue;
    }
    if (find_codec(type) == nullptr) {
      bool already_warned = false;
      if (context_.warned_types) {
        std::lock_guard<std::mutex> lock(mutex_);
        already_warned = context_.warned_types->count(type) > 0;
        if (!already_warned) {
          context_.warned_types->insert(type);
        }
      }
      if (!already_warned) {
        std::printf(
          "[bridge] uplink '%s' skipped: no codec for type '%s'\n", topic.c_str(), type.c_str());
      }
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (uplinks_.count(topic) > 0) {
        continue;
      }
    }
    auto uplink = std::make_shared<UplinkTopic>();
    uplink->topic = topic;
    uplink->type = type;
    uplink->running = true;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      uplinks_[topic] = uplink;
    }
    // The daemon's reader *is* this topic's local subscription, and nav2
    // decides whether to publish from the subscriber count: the controller
    // only writes /cmd_vel when `get_subscription_count() > 0`, and that
    // answer comes from the shared graph. Without this record the count is
    // zero on every machine here and the controller stays silent.
    ros2_shim::graph::ProcessRegistry::instance().add_topic(topic, type, "sub");
    uplink->worker = std::thread([this, uplink]() { uplinkLoop(uplink); });
    const std::string & peer = context_.peer != nullptr ? *context_.peer : std::string();
    std::printf("[bridge] forwarding %s [%s] to %s\n", topic.c_str(), type.c_str(), peer.c_str());
  }
}

std::string UplinkForwarderService::local_publisher_type(const std::string & topic)
{
  for (const auto & entry : ros2_shim::graph::list_topics()) {
    if (entry.name == topic && entry.source == "local" && entry.publishers > 0 &&
      !entry.type.empty())
    {
      return entry.type;
    }
  }
  return std::string();
}

void UplinkForwarderService::check_uplink_type(const std::shared_ptr<UplinkTopic> & uplink)
{
  const auto now = std::chrono::steady_clock::now();
  if (uplink->last_type_check.time_since_epoch().count() != 0 &&
    now - uplink->last_type_check < std::chrono::seconds(2))
  {
    return;
  }
  uplink->last_type_check = now;

  const std::string local_type = local_publisher_type(uplink->topic);
  if (local_type.empty() || local_type == uplink->type || local_type == uplink->warned_type) {
    return;
  }
  uplink->warned_type = local_type;
  std::fprintf(
    stderr,
    "[bridge] uplink '%s': the local publisher uses %s but the peer expects %s; "
    "a ROS2 topic has one type and these two do not decode into each other, so what "
    "arrives there is nonsense (for /cmd_vel: the robot never moves). Make the "
    "publisher agree - nav2 picks between geometry_msgs/msg/Twist and "
    "geometry_msgs/msg/TwistStamped with its `enable_stamped_cmd_vel` parameter.\n",
    uplink->topic.c_str(), local_type.c_str(), uplink->type.c_str());
}

void UplinkForwarderService::uplinkLoop(const std::shared_ptr<UplinkTopic> & uplink)
{
  std::vector<std::uint8_t> buffer(64 * 1024);
  while ((!context_.running || context_.running()) && uplink->running) {
    check_uplink_type(uplink);
    if (!uplink->channel) {
      std::string error;
      // Created rather than opened: a `Local\` mapping lives only as long as
      // some process holds it, so `ros2 topic pub --once /cmd_vel` creates
      // the channel, publishes and exits - taking the channel with it. As the
      // owner of this topic (it is advertised as remote in the graph) the
      // daemon creates it and keeps the handle, so a single `--once` publish
      // lands whether it happens before or after nav2 shows up.
      uplink->channel = create_channel(uplink->topic, &error, uplink->type);
      if (!uplink->channel) {
        if (!uplink->reported) {
          uplink->reported = true;
          std::fprintf(
            stderr, "[bridge] cannot open '%s' for forwarding: %s\n", uplink->topic.c_str(),
            error.c_str());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        continue;
      }
      buffer.assign(uplink->channel->slot_size(), 0);
    }

    std::size_t size = 0;
    if (!uplink->channel->subscribe(buffer.data(), buffer.size(), &size, 100)) {
      continue;
    }
    ros2_grpc_sdk::Bytes shim(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(size));
    ros2_grpc_sdk::Bytes cdr;
    if (!shim_to_cdr(uplink->type, shim, cdr)) {
      uplink->failures.fetch_add(1);
      continue;
    }
    const ros2_grpc_sdk::Status status =
      context_.client->publish(uplink->topic, uplink->type, cdr, "cdr");
    if (!status.ok()) {
      uplink->failures.fetch_add(1);
      continue;
    }
    uplink->messages.fetch_add(1);
  }
}

std::size_t UplinkForwarderService::uplink_count() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return uplinks_.size();
}

void UplinkForwarderService::append_report(std::vector<std::string> & lines) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto & entry : uplinks_) {
    const auto & uplink = entry.second;
    char buffer[256];
    std::snprintf(
      buffer, sizeof(buffer), "  ^ %-26s %-34s msgs=%-8llu failed=%llu",
      uplink->topic.c_str(), uplink->type.c_str(),
      static_cast<unsigned long long>(uplink->messages.load()),
      static_cast<unsigned long long>(uplink->failures.load()));
    lines.push_back(buffer);
  }
}

void UplinkForwarderService::stop()
{
  std::vector<std::shared_ptr<UplinkTopic>> uplinks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto & entry : uplinks_) {
      entry.second->running = false;
      uplinks.push_back(entry.second);
    }
  }
  for (const auto & uplink : uplinks) {
    if (uplink->worker.joinable()) {
      uplink->worker.join();
    }
    ros2_shim::graph::ProcessRegistry::instance().remove_topic(
      uplink->topic, uplink->type, "local");
  }
  std::lock_guard<std::mutex> lock(mutex_);
  uplinks_.clear();
}

}  // namespace ros2_bridge
