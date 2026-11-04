#include "ros2_bridge/topic_mirror.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

#include "ros2_bridge/shm_topic.hpp"
#include "ros2_bridge/type_registry.hpp"
#include "ros2_shim/graph.hpp"

namespace ros2_bridge
{
namespace
{

// The first re-subscribe is immediate - an agent that just restarted is
// reachable again within a second or two - and later ones back off so a
// robot that stays away does not cost a stream per refresh.
constexpr int kMirrorRetryBaseMs = 250;
constexpr int kMirrorRetryMaxMs = 8000;
// A stream that stayed up this long was healthy, so its next failure starts
// the backoff from the beginning again.
constexpr int kMirrorHealthyMs = 5000;

int mirror_backoff_ms(int failures)
{
  long long delay = kMirrorRetryBaseMs;
  for (int i = 1; i < failures && delay < kMirrorRetryMaxMs; ++i) {
    delay *= 2;
  }
  return static_cast<int>(delay > kMirrorRetryMaxMs ? kMirrorRetryMaxMs : delay);
}

}  // namespace

TopicMirrorService::TopicMirrorService(MirrorContext context)
: context_(std::move(context))
{
}

bool TopicMirrorService::sync()
{
  std::vector<ros2_grpc_sdk::TopicInfo> topics;
  const ros2_grpc_sdk::Status status = context_.client->listTopics(topics);
  if (!status.ok()) {
    if (context_.report_scan_failure) {
      context_.report_scan_failure("listTopics", status.message());
    }
    return false;
  }
  if (context_.clear_scan_failure) {
    context_.clear_scan_failure("listTopics");
  }

  const std::string & peer = context_.peer != nullptr ? *context_.peer : std::string();

  for (const auto & topic : topics) {
    if (topic.name.empty()) {
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      remote_topics_[topic.name] = topic;
      const auto existing = mirrors_.find(topic.name);
      if (existing != mirrors_.end()) {
        // A mirror whose stream is still running is left alone; a dead one
        // has to be dropped here so the code below subscribes again. Without
        // this the topic stayed in `mirrors_` forever, its shared-memory
        // channel stopped being written, and nav2 waited for an /odom that
        // never came - the daemon had to be restarted to get it back.
        if (existing->second->live.load(std::memory_order_acquire)) {
          continue;
        }
        if (!retireMirrorLocked(existing)) {
          continue;  // still backing off from an earlier failed re-subscribe
        }
      }
    }
    // Topics that travel *from* Windows (nav2's /cmd_vel, ...) must never be
    // mirrored: a shim channel has a single writer, and a mirrored /cmd_vel
    // would feed the daemon's own uplink straight back to the robot.
    const auto & uplink = context_.uplink_topics != nullptr ?
      *context_.uplink_topics : std::vector<std::string>();
    if (std::find(uplink.begin(), uplink.end(), topic.name) != uplink.end()) {
      // Still advertise it, so `ros2 topic list` shows the topic and a local
      // `ros2 topic pub` knows which shared-memory channel to write into.
      ros2_shim::graph::ProcessRegistry::instance().set_remote_topic(
        topic.name, topic.type, "remote:" + peer);
      continue;
    }
    // A topic the peer merely subscribes to has nothing to mirror.
    if (topic.publisher_count == 0 && topic.subscriber_count > 0) {
      continue;
    }
    if (!context_.uplink_topics) {
      continue;
    }
    if (find_codec(topic.type) == nullptr) {
      bool already_warned = false;
      if (context_.warned_types) {
        std::lock_guard<std::mutex> lock(mutex_);
        already_warned = context_.warned_types->count(topic.type) > 0;
        if (!already_warned) {
          context_.warned_types->insert(topic.type);
        }
      }
      if (!already_warned) {
        std::printf(
          "[bridge] skipping '%s': no codec for type '%s'\n", topic.name.c_str(),
          topic.type.c_str());
      }
      continue;
    }
    startMirror(topic);
  }

  // A topic whose last publisher went away disappears from the agent's list.
  // Its mirror would otherwise stay in `mirrors_` with live=false forever,
  // and the ready file only claims "ready" when every mirror is live - so one
  // stale topic (a demo publisher the user stopped, a bag that ended) kept
  // `ros2.bat` waiting its full thirty seconds on every single invocation.
  // Drop the graph record and the mirror together; a topic that comes back
  // is re-created by the loop above on the next refresh.
  std::set<std::string> advertised;
  for (const auto & topic : topics) {
    advertised.insert(topic.name);
  }
  std::vector<std::string> stale;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto & entry : mirrors_) {
      if (advertised.count(entry.first) == 0) {
        stale.push_back(entry.first);
      }
    }
    for (const auto & name : stale) {
      mirrors_.erase(name);
      remote_topics_.erase(name);
      retries_.erase(name);
    }
  }
  for (const auto & name : stale) {
    ros2_shim::graph::ProcessRegistry::instance().remove_topic(
      name, "", "remote:" + peer);
    std::fprintf(
      stderr, "[bridge] dropping '%s' (the agent no longer advertises it)\n",
      name.c_str());
  }
  return true;
}

bool TopicMirrorService::retireMirrorLocked(
  std::map<std::string, std::shared_ptr<TopicMirror>>::iterator it)
{
  // A copy, not a reference: the erase below would dangle it.
  const std::shared_ptr<TopicMirror> mirror = it->second;
  const auto now = std::chrono::steady_clock::now();
  MirrorRetry & retry = retries_[mirror->topic];
  const auto lived = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - mirror->started_at).count();
  // Only an early end counts as a failure: a stream that ran for a while and
  // then died (the usual restart) is not a symptom of a bad re-subscribe.
  if (lived >= kMirrorHealthyMs) {
    retry.failures = 0;
  }
  if (now < retry.next_attempt) {
    return false;
  }
  retry.failures += 1;
  retry.next_attempt = now + std::chrono::milliseconds(mirror_backoff_ms(retry.failures));
  mirrors_.erase(it);
  std::fprintf(
    stderr, "[bridge] re-subscribing %s (stream ended after %lld ms)\n",
    mirror->topic.c_str(), static_cast<long long>(lived));
  return true;
}

void TopicMirrorService::startMirror(const ros2_grpc_sdk::TopicInfo & topic)
{
  auto mirror = std::make_shared<TopicMirror>();
  mirror->topic = topic.name;
  mirror->type = topic.type;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto retry = retries_.find(topic.name);
    if (retry != retries_.end()) {
      mirror->reconnects.store(retry->second.reconnects, std::memory_order_relaxed);
    }
  }

  std::string error;
  mirror->channel = create_channel(topic.name, &error, topic.type);
  if (!mirror->channel) {
    std::fprintf(
      stderr, "[bridge] cannot create shared memory for '%s': %s\n", topic.name.c_str(),
      error.c_str());
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mirrors_.count(topic.name) > 0) {
      return;
    }
    mirrors_[topic.name] = mirror;
  }

  const std::string & peer = context_.peer != nullptr ? *context_.peer : std::string();
  ros2_shim::graph::ProcessRegistry::instance().set_remote_topic(
    topic.name, topic.type, "remote:" + peer);

  const ros2_grpc_sdk::Status status = context_.client->subscribe(
    topic.name, topic.type,
    [mirror](const ros2_grpc_sdk::Message & message) {
      onRemoteMessage(mirror, message);
    },
    nullptr,
    [mirror](const ros2_grpc_sdk::Status & status) {
      // The stream is gone: flag the mirror so the next refresh rebuilds it.
      // Only the flag and a counter are touched here - this runs on the
      // subscription's own worker thread, which the refresh thread joins
      // while it re-subscribes, so it must not take any lock.
      mirror->live.store(false, std::memory_order_release);
      std::fprintf(
        stderr, "[bridge] subscription for '%s' closed: %s\n", mirror->topic.c_str(),
        status.toString().c_str());
    },
    0);

  if (!status.ok()) {
    std::fprintf(
      stderr, "[bridge] subscribe '%s' failed: %s\n", topic.name.c_str(),
      status.message().c_str());
    std::lock_guard<std::mutex> lock(mutex_);
    mirrors_.erase(topic.name);
    // Counted like an early stream end, so the retry backs off instead of
    // hammering an agent that is not answering subscriptions yet.
    MirrorRetry & retry = retries_[topic.name];
    retry.failures += 1;
    retry.next_attempt = std::chrono::steady_clock::now() +
      std::chrono::milliseconds(mirror_backoff_ms(retry.failures));
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    MirrorRetry & retry = retries_[topic.name];
    if (retry.failures > 0) {
      retry.reconnects += 1;
      mirror->reconnects.store(retry.reconnects, std::memory_order_relaxed);
      retry.next_attempt = std::chrono::steady_clock::time_point{};
    }
  }
  std::printf(
    "[bridge] mirroring %s [%s] -> shm '%s'\n", topic.name.c_str(), topic.type.c_str(),
    channel_name(topic.name).c_str());
}

void TopicMirrorService::onRemoteMessage(
  const std::shared_ptr<TopicMirror> & mirror, const ros2_grpc_sdk::Message & message)
{
  ros2_grpc_sdk::Bytes shim;
  if (message.encoding.empty() || message.encoding == "cdr") {
    if (!cdr_to_shim(mirror->type, message.data, shim)) {
      mirror->failures.fetch_add(1);
      return;
    }
  } else if (message.encoding == "shim" || message.encoding == "nav2shim") {
    shim = message.data;
  } else {
    mirror->failures.fetch_add(1);
    return;
  }

  if (!mirror->channel->publish(shim.data(), shim.size())) {
    mirror->failures.fetch_add(1);
    return;
  }
  mirror->messages.fetch_add(1);
  mirror->bytes.fetch_add(shim.size());
  mirror->last_stamp_ns.store(message.stamp_ns);
}

std::size_t TopicMirrorService::remote_topic_count() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return remote_topics_.size();
}

bool TopicMirrorService::remote_type_of(const std::string & topic, std::string & type) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = remote_topics_.find(topic);
  if (it == remote_topics_.end()) {
    return false;
  }
  type = it->second.type;
  return true;
}

std::size_t TopicMirrorService::mirror_count() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return mirrors_.size();
}

std::size_t TopicMirrorService::live_mirror_count() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::size_t live = 0;
  for (const auto & entry : mirrors_) {
    if (entry.second->live.load(std::memory_order_acquire)) {
      live += 1;
    }
  }
  return live;
}

void TopicMirrorService::append_report(std::vector<std::string> & lines) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto & entry : mirrors_) {
    const auto & mirror = entry.second;
    char buffer[256];
    std::snprintf(
      buffer, sizeof(buffer),
      "  mirror %-28s %-34s %8llu msg %10llu B %6llu fail %s",
      mirror->topic.c_str(), mirror->type.c_str(),
      static_cast<unsigned long long>(mirror->messages.load()),
      static_cast<unsigned long long>(mirror->bytes.load()),
      static_cast<unsigned long long>(mirror->failures.load()),
      mirror->live.load(std::memory_order_acquire) ? "live" : "dead");
    lines.push_back(buffer);
  }
}

void TopicMirrorService::stop()
{
  std::vector<std::string> names;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto & entry : mirrors_) {
      names.push_back(entry.first);
    }
    mirrors_.clear();
    retries_.clear();
    remote_topics_.clear();
  }
  const std::string & peer = context_.peer != nullptr ? *context_.peer : std::string();
  for (const auto & name : names) {
    ros2_shim::graph::ProcessRegistry::instance().remove_topic(
      name, "", "remote:" + peer);
  }
}

}  // namespace ros2_bridge
