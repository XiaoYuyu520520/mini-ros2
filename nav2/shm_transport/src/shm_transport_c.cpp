#include "shm_transport/shm_transport_c.h"

#include "shm_transport/shm_channel.hpp"

#include <memory>

using shm_transport::ShmChannel;

struct shm_channel_s
{
  std::unique_ptr<ShmChannel> channel;
};

shm_channel_t shm_channel_create(const char * name, uint32_t slot_count, uint32_t slot_size)
{
  if (!name) {
    return nullptr;
  }
  auto ch = ShmChannel::create(name, slot_count, slot_size);
  if (!ch) {
    return nullptr;
  }
  auto * handle = new shm_channel_s();
  handle->channel = std::move(ch);
  return handle;
}

shm_channel_t shm_channel_open(const char * name)
{
  if (!name) {
    return nullptr;
  }
  auto ch = ShmChannel::open(name);
  if (!ch) {
    return nullptr;
  }
  auto * handle = new shm_channel_s();
  handle->channel = std::move(ch);
  return handle;
}

int shm_channel_publish(shm_channel_t ch, const void * data, size_t size)
{
  if (!ch || !ch->channel) {
    return -1;
  }
  return ch->channel->publish(data, size) ? 0 : -1;
}

int shm_channel_subscribe(
  shm_channel_t ch, void * out, size_t capacity, size_t * out_size, int timeout_ms)
{
  if (!ch || !ch->channel) {
    return -1;
  }
  return ch->channel->subscribe(out, capacity, out_size, timeout_ms) ? 0 : -1;
}

uint64_t shm_channel_published_count(shm_channel_t ch)
{
  return (ch && ch->channel) ? ch->channel->published_count() : 0;
}

uint64_t shm_channel_read_count(shm_channel_t ch)
{
  return (ch && ch->channel) ? ch->channel->read_count() : 0;
}

uint64_t shm_channel_dropped_count(shm_channel_t ch)
{
  return (ch && ch->channel) ? ch->channel->dropped_count() : 0;
}

void shm_channel_close(shm_channel_t ch)
{
  delete ch;
}
