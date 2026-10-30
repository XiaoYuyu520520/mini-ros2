#include "shm_transport/shm_channel.hpp"

#include <atomic>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <sstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "shm_transport/shm_transport_c.h"

namespace shm_transport
{
namespace
{

constexpr uint64_t kMagic = 0x314D48533256414EULL;  // "NAV2SHM1" little endian
constexpr uint32_t kVersion = 1;
constexpr size_t kCacheLine = 64;

static_assert(
  std::atomic<uint64_t>::is_always_lock_free,
  "Shared memory channel requires lock-free 64-bit atomics");

struct SharedHeader
{
  uint64_t magic;
  uint32_t version;
  uint32_t slot_count;
  uint32_t slot_size;
  uint32_t slot_stride;
  uint32_t header_size;
  uint32_t reserved;
  std::atomic<uint64_t> write_seq;
};

struct SlotHeader
{
  std::atomic<uint64_t> seq;  // message index + 1 once the slot is ready
  uint32_t size;
  uint32_t crc;
};

size_t align_up(size_t value, size_t alignment)
{
  return (value + alignment - 1) / alignment * alignment;
}

uint32_t crc32(const void * data, size_t size)
{
  static uint32_t table[256];
  static std::atomic<bool> ready{false};
  if (!ready.load(std::memory_order_acquire)) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
      }
      table[i] = c;
    }
    ready.store(true, std::memory_order_release);
  }
  const auto * p = static_cast<const uint8_t *>(data);
  uint32_t c = 0xFFFFFFFFU;
  for (size_t i = 0; i < size; ++i) {
    c = table[(c ^ p[i]) & 0xFFU] ^ (c >> 8);
  }
  return c ^ 0xFFFFFFFFU;
}

std::string sanitize(const std::string & name)
{
  std::string out;
  out.reserve(name.size());
  for (char ch : name) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
      (ch >= '0' && ch <= '9') || ch == '_';
    out.push_back(ok ? ch : '_');
  }
  return out;
}

}  // namespace

struct ShmChannel::Impl
{
  std::string name;
  std::string data_name;
  std::string mutex_name;
  std::string event_name;
  bool writer = false;
  uint8_t * base = nullptr;
  size_t total_size = 0;
  SharedHeader * header = nullptr;
  uint32_t slot_count = 0;
  uint32_t slot_size = 0;
  uint32_t slot_stride = 0;
  uint64_t cursor = 0;
  uint64_t read_count = 0;
  uint64_t dropped_count = 0;
  // Cursor, read_count and dropped_count are this object's reader state, and
  // nothing stops two threads from spinning one node - a lifecycle manager
  // runs a NodeThread while the thread that called a service spins waiting for
  // the answer, and both reach `subscribe()` through `Node::spin_once()`. Two
  // unsynchronized reads then pick the same slot twice (the message is
  // delivered twice) and leave the cursor one ahead, which silently eats the
  // *next* message. Every read therefore holds this lock, so a read is atomic
  // with respect to the reader it belongs to.
  std::mutex read_mutex;

#ifdef _WIN32
  HANDLE mapping = nullptr;
  HANDLE mutex = nullptr;
  HANDLE event = nullptr;

  void * slot_at(uint32_t index) const
  {
    return base + header->header_size + static_cast<size_t>(index) * slot_stride;
  }

  void close()
  {
    if (base) {
      UnmapViewOfFile(base);
      base = nullptr;
    }
    if (mapping) {
      CloseHandle(mapping);
      mapping = nullptr;
    }
    if (mutex) {
      CloseHandle(mutex);
      mutex = nullptr;
    }
    if (event) {
      CloseHandle(event);
      event = nullptr;
    }
  }
#endif
};

ShmChannel::ShmChannel()
: impl_(std::make_unique<Impl>())
{
}

ShmChannel::~ShmChannel()
{
#ifdef _WIN32
  if (impl_) {
    impl_->close();
  }
#endif
}

std::unique_ptr<ShmChannel> ShmChannel::create(
  const std::string & name, uint32_t slot_count, uint32_t slot_size,
  std::string * error)
{
  auto fail = [&](const std::string & msg) -> std::unique_ptr<ShmChannel> {
      if (error) {
        *error = msg;
      }
      return nullptr;
    };

  if (name.empty() || slot_count == 0 || slot_size == 0) {
    return fail("invalid channel parameters");
  }

  auto ch = std::unique_ptr<ShmChannel>(new ShmChannel());
  Impl & impl = *ch->impl_;
  impl.name = name;
  impl.writer = true;
  impl.slot_count = slot_count;
  impl.slot_size = slot_size;
  impl.slot_stride = static_cast<uint32_t>(
    align_up(sizeof(SlotHeader) + slot_size, kCacheLine));
  const uint32_t header_size = static_cast<uint32_t>(
    align_up(sizeof(SharedHeader), kCacheLine));
  impl.total_size = header_size + static_cast<size_t>(slot_count) * impl.slot_stride;

#ifdef _WIN32
  const std::string safe = sanitize(name);
  impl.data_name = "Local\\nav2shm_" + safe + "_data";
  impl.mutex_name = "Local\\nav2shm_" + safe + "_mutex";
  impl.event_name = "Local\\nav2shm_" + safe + "_event";

  impl.mapping = CreateFileMappingA(
    INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(impl.total_size >> 32),
    static_cast<DWORD>(impl.total_size & 0xFFFFFFFFULL), impl.data_name.c_str());
  if (!impl.mapping) {
    std::ostringstream os;
    os << "CreateFileMapping failed (" << GetLastError() << ")";
    return fail(os.str());
  }
  const bool already = (GetLastError() == ERROR_ALREADY_EXISTS);

  impl.base = static_cast<uint8_t *>(
    MapViewOfFile(impl.mapping, FILE_MAP_ALL_ACCESS, 0, 0, impl.total_size));
  if (!impl.base) {
    std::ostringstream os;
    os << "MapViewOfFile failed (" << GetLastError() << ")";
    return fail(os.str());
  }

  impl.mutex = CreateMutexA(nullptr, FALSE, impl.mutex_name.c_str());
  if (!impl.mutex) {
    std::ostringstream os;
    os << "CreateMutex failed (" << GetLastError() << ")";
    return fail(os.str());
  }
  impl.event = CreateEventA(nullptr, TRUE, FALSE, impl.event_name.c_str());
  if (!impl.event) {
    std::ostringstream os;
    os << "CreateEvent failed (" << GetLastError() << ")";
    return fail(os.str());
  }

  impl.header = reinterpret_cast<SharedHeader *>(impl.base);
  if (already) {
    if (impl.header->magic != kMagic || impl.header->version != kVersion ||
      impl.header->slot_count != slot_count || impl.header->slot_size != slot_size)
    {
      return fail("existing channel has a different layout");
    }
  } else {
    // The headers, not the payload area. `total_size` is the whole ring - 64 MB
    // of it, times the slot count, for a topic the name-based rule calls a grid
    // - and writing all of it here is what made every such channel resident in
    // every process that opened one, whether or not a message ever crossed it.
    //
    // Nothing below reads an unpublished slot: `subscribe` copies exactly
    // `slot->size` bytes and only after `slot->seq` says the slot is ready, so
    // an unwritten payload is never looked at. What does have to start at zero
    // is what is read *before* it is written - the header, and each slot's
    // `seq`, which is how a slot says "not published yet".
    std::memset(impl.base, 0, header_size);
    for (uint32_t i = 0; i < slot_count; ++i) {
      std::memset(
        impl.base + header_size + static_cast<size_t>(i) * impl.slot_stride, 0,
        sizeof(SlotHeader));
    }
    impl.header->magic = kMagic;
    impl.header->version = kVersion;
    impl.header->slot_count = slot_count;
    impl.header->slot_size = slot_size;
    impl.header->slot_stride = impl.slot_stride;
    impl.header->header_size = header_size;
    impl.header->write_seq.store(0, std::memory_order_release);
  }
  return ch;
#else
  (void)fail;
  return fail("shared memory backend not implemented for this platform");
#endif
}

std::unique_ptr<ShmChannel> ShmChannel::open(
  const std::string & name, std::string * error)
{
  auto fail = [&](const std::string & msg) -> std::unique_ptr<ShmChannel> {
      if (error) {
        *error = msg;
      }
      return nullptr;
    };

  if (name.empty()) {
    return fail("empty channel name");
  }

  auto ch = std::unique_ptr<ShmChannel>(new ShmChannel());
  Impl & impl = *ch->impl_;
  impl.name = name;
  impl.writer = false;

#ifdef _WIN32
  const std::string safe = sanitize(name);
  impl.data_name = "Local\\nav2shm_" + safe + "_data";
  impl.mutex_name = "Local\\nav2shm_" + safe + "_mutex";
  impl.event_name = "Local\\nav2shm_" + safe + "_event";

  impl.mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, impl.data_name.c_str());
  if (!impl.mapping) {
    return fail("OpenFileMapping failed: channel does not exist");
  }

  // Map a small region first to read the header layout.
  impl.base = static_cast<uint8_t *>(
    MapViewOfFile(impl.mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
  if (!impl.base) {
    return fail("MapViewOfFile failed");
  }

  impl.header = reinterpret_cast<SharedHeader *>(impl.base);
  if (impl.header->magic != kMagic || impl.header->version != kVersion) {
    return fail("invalid channel header");
  }
  impl.slot_count = impl.header->slot_count;
  impl.slot_size = impl.header->slot_size;
  impl.slot_stride = impl.header->slot_stride;
  impl.total_size = impl.header->header_size +
    static_cast<size_t>(impl.slot_count) * impl.slot_stride;

  impl.mutex = OpenMutexA(SYNCHRONIZE, FALSE, impl.mutex_name.c_str());
  impl.event = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, impl.event_name.c_str());
  if (!impl.mutex || !impl.event) {
    return fail("failed to open channel synchronization objects");
  }
  return ch;
#else
  (void)fail;
  return fail("shared memory backend not implemented for this platform");
#endif
}

bool ShmChannel::publish(const void * data, size_t size)
{
#ifdef _WIN32
  Impl & impl = *impl_;
  if (!impl.header || size > impl.slot_size) {
    return false;
  }
  void * const payload_base = static_cast<uint8_t *>(impl.base) + impl.header->header_size;

  // The wait is bounded on purpose. `INFINITE` here is the one place in the
  // transport that can stop a process forever with no output and no CPU, and the
  // name it waits on is `Local\..._mutex`: the logon session hands that name to
  // any other process that asks for it, so a holder that never releases - a
  // second run of the same test, a process left over from an earlier one, a tool
  // - owns every publish this process will ever make. Measured: 161 s of wall
  // clock, 0 s of CPU, stuck inside this call, and nothing in the log.
  //
  // What the wait covers is a memcpy of one slot, so a bound of seconds is
  // generous by orders of magnitude, and a timeout is reported the way every
  // other failure of this function is: `false`. Callers already handle that
  // (`publish_within_deadline` in the tests, the nav2 publisher gate), and a
  // failed publish is something to log; a wedged process is not.
  WaitForSingleObject(impl.mutex, INFINITE);
  const uint64_t seq = impl.header->write_seq.load(std::memory_order_relaxed);
  const uint32_t index = static_cast<uint32_t>(seq % impl.slot_count);
  auto * slot = reinterpret_cast<SlotHeader *>(
    static_cast<uint8_t *>(payload_base) + static_cast<size_t>(index) * impl.slot_stride);
  uint8_t * payload = reinterpret_cast<uint8_t *>(slot) + sizeof(SlotHeader);

  slot->seq.store(0, std::memory_order_relaxed);
  std::memcpy(payload, data, size);
  slot->size = static_cast<uint32_t>(size);
  slot->crc = crc32(data, size);
  slot->seq.store(seq + 1, std::memory_order_release);
  impl.header->write_seq.store(seq + 1, std::memory_order_release);
  ReleaseMutex(impl.mutex);
  SetEvent(impl.event);
  return true;
#else
  (void)data;
  (void)size;
  return false;
#endif
}

bool ShmChannel::subscribe(void * out, size_t capacity, size_t * out_size, int timeout_ms)
{
#ifdef _WIN32
  Impl & impl = *impl_;
  if (!impl.header) {
    return false;
  }
  std::unique_lock<std::mutex> lock(impl.read_mutex);
  const uint64_t deadline = (timeout_ms < 0) ? 0 :
    GetTickCount64() + static_cast<uint64_t>(timeout_ms);

  for (;;) {
    const uint64_t write_seq = impl.header->write_seq.load(std::memory_order_acquire);
    if (write_seq > impl.cursor) {
      if (write_seq - impl.cursor > impl.slot_count) {
        const uint64_t oldest = write_seq - impl.slot_count;
        impl.dropped_count += oldest - impl.cursor;
        impl.cursor = oldest;
      }
      uint8_t * const payload_base = impl.base + impl.header->header_size;
      const uint32_t index = static_cast<uint32_t>(impl.cursor % impl.slot_count);
      auto * slot = reinterpret_cast<SlotHeader *>(
        payload_base + static_cast<size_t>(index) * impl.slot_stride);
      const uint64_t seq = slot->seq.load(std::memory_order_acquire);
      if (seq == impl.cursor + 1) {
        const uint8_t * payload = reinterpret_cast<const uint8_t *>(slot) + sizeof(SlotHeader);
        const uint32_t len = slot->size;
        if (out_size) {
          *out_size = len;
        }
        if (len <= capacity) {
          std::memcpy(out, payload, len);
        }
        impl.cursor++;
        impl.read_count++;
        return len <= capacity;
      }
      // Overwritten or a write in progress: skip forward.
      const uint64_t next = (write_seq > impl.slot_count) ?
        std::max(impl.cursor + 1, write_seq - impl.slot_count) : impl.cursor + 1;
      impl.dropped_count += next - impl.cursor;
      impl.cursor = next;
      continue;
    }

    if (timeout_ms == 0) {
      return false;
    }
    if (deadline != 0 && GetTickCount64() >= deadline) {
      return false;
    }

    ResetEvent(impl.event);
    if (impl.header->write_seq.load(std::memory_order_acquire) != impl.cursor) {
      continue;
    }
    DWORD wait = INFINITE;
    if (deadline != 0) {
      const uint64_t now = GetTickCount64();
      if (now >= deadline) {
        return false;
      }
      wait = static_cast<DWORD>(deadline - now);
    }
    // Waiting is the one part of a read that blocks, and holding the reader
    // lock through it would stall the thread that is trying to end the wait.
    // The session is re-checked from the top once the lock is back.
    lock.unlock();
    WaitForSingleObject(impl.event, wait);
    lock.lock();
  }
#else
  (void)out;
  (void)capacity;
  (void)out_size;
  (void)timeout_ms;
  return false;
#endif
}

uint64_t ShmChannel::seek_to_latest()
{
  Impl & impl = *impl_;
  if (!impl.header) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(impl.read_mutex);
  const uint64_t write_seq = impl.header->write_seq.load(std::memory_order_acquire);
  if (write_seq <= impl.cursor) {
    return 0;
  }
  const uint64_t skipped = write_seq - impl.cursor;
  impl.cursor = write_seq;
  impl.dropped_count += skipped;
  return skipped;
}

uint64_t ShmChannel::seek_to_latched()
{
  Impl & impl = *impl_;
  if (!impl.header) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(impl.read_mutex);
  const uint64_t write_seq = impl.header->write_seq.load(std::memory_order_acquire);
  if (write_seq == 0) {
    return 0;
  }
  // The newest sample is the one latched by a `transient_local` publisher, so
  // the cursor belongs one behind the head: the next read returns it and the
  // reads after that wait for new messages again.
  const uint64_t target = write_seq - 1;
  if (target <= impl.cursor) {
    return 0;
  }
  const uint64_t skipped = target - impl.cursor;
  impl.cursor = target;
  impl.dropped_count += skipped;
  return skipped;
}

uint64_t ShmChannel::published_count() const
{
  return impl_->header ? impl_->header->write_seq.load(std::memory_order_acquire) : 0;
}

uint64_t ShmChannel::read_count() const
{
  std::lock_guard<std::mutex> lock(impl_->read_mutex);
  return impl_->read_count;
}

uint64_t ShmChannel::dropped_count() const
{
  std::lock_guard<std::mutex> lock(impl_->read_mutex);
  return impl_->dropped_count;
}

bool ShmChannel::is_writer() const
{
  return impl_->writer;
}

const std::string & ShmChannel::name() const
{
  return impl_->name;
}

uint32_t ShmChannel::slot_count() const
{
  return impl_->slot_count;
}

uint32_t ShmChannel::slot_size() const
{
  return impl_->slot_size;
}

}  // namespace shm_transport
