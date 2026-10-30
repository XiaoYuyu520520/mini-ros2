#include "shm_transport/shm_channel.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
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
#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <pthread.h>
#include <sys/file.h>
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

constexpr size_t align_up(size_t value, size_t alignment)
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

// The clock the waits below are measured on. One spelling for both platforms:
// the Windows side used to read GetTickCount64() and the POSIX side would have
// had to add a third one, and a monotonic millisecond count is what a timeout
// means on all of them.
uint64_t mono_ms()
{
  return static_cast<uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

// How long `publish` waits for the writer lock before it gives up and returns
// false. See the comment on that wait in `ShmChannel::publish`.
constexpr uint32_t kMutexWaitMs = 5000;

#ifdef _WIN32

// The header area is the layout header alone: the two objects the transport
// waits on are kernel objects with names of their own.
constexpr size_t kHeaderSize = align_up(sizeof(SharedHeader), kCacheLine);

#else

// POSIX has no named mutex or named event to hang off a channel name, so the
// objects both sides have to share live in the segment itself, right after the
// layout header, and every process reaches them by mapping the segment.
//
// There are two, and the second one is a pair:
//
//  * `write_mutex` serialises publishers. One writer per channel is the
//    contract, but nothing enforces it across processes, and the read-modify-
//    write of `write_seq` below is not atomic on its own.
//  * `wait_mutex`/`wait_cond` are the manual-reset event of the Windows side.
//    A semaphore cannot stand in for it: sem_post wakes one waiter, and a
//    channel with two subscribers has to wake both.
struct ShmSync
{
  // Two words nothing reads. They used to be a count of the live ShmChannel
  // objects holding the segment, which is what decided whether the last one
  // out unlinked it - and a count cannot answer that question: a process that
  // is killed does not get to decrement it, so a channel whose users had all
  // been killed could never be cleaned up, and every later `create` adopted
  // the corpse and everything in it, including a mutex its dead owner had
  // left marked not recoverable. That question is now put to an advisory lock
  // the kernel takes back when the process dies (see `Impl::create`); the
  // words stay so the sync block keeps the size it had.
  std::atomic<uint32_t> reserved0;
  std::atomic<uint32_t> reserved1;
  pthread_mutex_t write_mutex;
  pthread_mutex_t wait_mutex;
  pthread_cond_t wait_cond;
};

constexpr size_t kSyncOffset = align_up(sizeof(SharedHeader), kCacheLine);
constexpr size_t kHeaderSize = align_up(kSyncOffset + sizeof(ShmSync), kCacheLine);

std::string errno_text(const char * what, int code)
{
  std::ostringstream os;
  os << what << " failed: " << std::strerror(code) << " (" << code << ")";
  return os.str();
}

// `pthread_cond_timedwait` wants an absolute CLOCK_REALTIME time while the
// deadlines here are monotonic milliseconds.
timespec realtime_after(int64_t ms)
{
  timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += static_cast<time_t>(ms / 1000);
  ts.tv_nsec += static_cast<long>(ms % 1000) * 1000000L;
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_nsec -= 1000000000L;
    ts.tv_sec += 1;
  }
  return ts;
}

#endif

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

  void * slot_at(uint32_t index) const
  {
    return base + header->header_size + static_cast<size_t>(index) * slot_stride;
  }

#ifdef _WIN32
  HANDLE mapping = nullptr;
  HANDLE mutex = nullptr;
  HANDLE event = nullptr;

  // Writer side: take the publish mutex, or give up. WAIT_ABANDONED still
  // grants ownership - the previous holder exited without releasing it - so it
  // proceeds like WAIT_OBJECT_0 does; a timeout, or any other outcome, is a
  // publish that returns false.
  bool lock_writer()
  {
    const DWORD waited = WaitForSingleObject(mutex, kMutexWaitMs);
    return waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED;
  }

  void unlock_writer()
  {
    ReleaseMutex(mutex);
  }

  // The manual-reset event: publish sets it, a reader that found nothing to
  // read resets it, re-checks `write_seq` and waits.
  void wake_readers()
  {
    SetEvent(event);
  }

  void prepare_wait()
  {
    ResetEvent(event);
  }

  void wait_for_wakeup(uint64_t deadline, uint64_t cursor)
  {
    // The caller re-checked the cursor against `write_seq` right before the
    // reader lock was dropped, so the event is the only thing left to wait on.
    (void)cursor;
    DWORD wait = INFINITE;
    if (deadline != 0) {
      const uint64_t now = mono_ms();
      if (now >= deadline) {
        return;
      }
      wait = static_cast<DWORD>(deadline - now);
    }
    WaitForSingleObject(event, wait);
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
#else
  int fd = -1;
  size_t map_size = 0;
  ShmSync * sync = nullptr;

  // The same bounded wait the Windows side makes, against a mutex that lives
  // in the segment and is created robust: a publisher that dies while holding
  // it - kill -9, a crash, a leftover process from an earlier run - leaves
  // EOWNERDEAD for the next one instead of a lock that never opens again.
  // EOWNERDEAD hands ownership over all the same, so it proceeds, after
  // `pthread_mutex_consistent` puts the mutex back in a state every process
  // can use again.
  bool lock_writer()
  {
    if (sync == nullptr) {
      return false;
    }
    const timespec abs = realtime_after(kMutexWaitMs);
    for (;;) {
      const int rc = pthread_mutex_timedlock(&sync->write_mutex, &abs);
      if (rc == 0) {
        return true;
      }
      if (rc == EOWNERDEAD) {
        pthread_mutex_consistent(&sync->write_mutex);
        return true;
      }
      if (rc == EINTR) {
        continue;
      }
      return false;
    }
  }

  void unlock_writer()
  {
    pthread_mutex_unlock(&sync->write_mutex);
  }

  // The manual-reset event, spelled as a condition variable. A publisher holds
  // `wait_mutex` across the broadcast, so a reader cannot look at the
  // predicate between the store of `write_seq` and the wakeup and then sleep
  // through it.
  //
  // The mutex is robust for the same reason `write_mutex` is, and this side of
  // the transport has to say so out loud: EOWNERDEAD is not a failure to lock,
  // it is the lock being handed over from a publisher that died holding it,
  // and it is only released again by a caller that first puts the mutex back
  // into a consistent state. Treating it as "the lock did not come" and
  // walking away - which is what this did - leaves `wait_mutex` owned by a
  // thread that does not know it owns anything. The next `publish` then waits
  // for that thread to release it, which is itself: measured, the writer sat
  // in `pthread_mutex_lock` with `__owner` left at glibc's not-recoverable
  // sentinel, at 0% CPU, for good, while every reader timed out against the
  // same mutex. Two runs in a row of the throughput case below reproduce it.
  void wake_readers()
  {
    if (sync == nullptr) {
      return;
    }
    const int rc = pthread_mutex_lock(&sync->wait_mutex);
    if (rc == EOWNERDEAD) {
      // Nothing was published between the owner's death and now, so the
      // broadcast below is exactly the one it would have made itself.
      pthread_mutex_consistent(&sync->wait_mutex);
    } else if (rc != 0) {
      // A mutex that cannot be locked at all (ENOTRECOVERABLE) costs this
      // publish one wakeup and nothing else: `write_seq` is already stored,
      // and a reader that is waiting re-reads it on its own timeout. Wedging
      // the publisher instead would turn a lost wakeup into a lost channel.
      return;
    }
    pthread_cond_broadcast(&sync->wait_cond);
    pthread_mutex_unlock(&sync->wait_mutex);
  }

  // A condition variable keeps no latched state for a wait to consume, so
  // there is nothing to reset here. What replaces the reset is the predicate
  // check inside `wait_for_wakeup`, which happens under `wait_mutex`.
  void prepare_wait()
  {
  }

  void wait_for_wakeup(uint64_t deadline, uint64_t cursor)
  {
    if (sync == nullptr) {
      return;
    }
    const bool bounded = (deadline != 0);
    timespec abs{};
    if (bounded) {
      const uint64_t now = mono_ms();
      if (now >= deadline) {
        return;
      }
      abs = realtime_after(static_cast<int64_t>(deadline - now));
    }
    const int locked = bounded ?
      pthread_mutex_timedlock(&sync->wait_mutex, &abs) :
      pthread_mutex_lock(&sync->wait_mutex);
    if (locked != 0) {
      if (locked == EOWNERDEAD) {
        pthread_mutex_consistent(&sync->wait_mutex);
      } else {
        // A timeout while waiting for the mutex: the deadline passed, and the
        // caller re-checks it from the top of its loop.
        return;
      }
    }
    while (header->write_seq.load(std::memory_order_acquire) == cursor) {
      const int rc = bounded ?
        pthread_cond_timedwait(&sync->wait_cond, &sync->wait_mutex, &abs) :
        pthread_cond_wait(&sync->wait_cond, &sync->wait_mutex);
      if (rc == EOWNERDEAD) {
        pthread_mutex_consistent(&sync->wait_mutex);
        continue;
      }
      if (rc == ETIMEDOUT || (rc != 0 && rc != EINTR)) {
        break;
      }
      // A signal or a spurious wakeup: the loop re-reads the predicate.
    }
    pthread_mutex_unlock(&sync->wait_mutex);
  }

  void close()
  {
    if (base != nullptr) {
      munmap(base, map_size);
      base = nullptr;
      header = nullptr;
      sync = nullptr;
    }
    if (fd >= 0) {
      // A Windows named object is destroyed by the kernel when its last
      // handle closes. A POSIX segment outlives every process that mapped it,
      // so this lock is what makes the two behave alike: it is held shared for
      // as long as a channel is open, and the kernel drops it when the process
      // ends. Taking it exclusively therefore succeeds only when this really
      // is the last user - the others closed the channel, or were killed,
      // which is the case the lock exists for - and only then may the name go
      // away. Failure means somebody else still has the channel open, and the
      // segment stays for them. Note that "somebody else" includes this
      // process: an flock belongs to an open file description, so a second
      // ShmChannel on the same name holds a lock of its own.
      const bool last_user = (flock(fd, LOCK_EX | LOCK_NB) == 0);
      if (last_user && named_object_is_ours()) {
        ::shm_unlink(data_name.c_str());
      }
      ::close(fd);
      fd = -1;
    }
  }

  // True when `data_name` still resolves to the object `fd` holds. Between the
  // exclusive lock above and the unlink, another process can unlink this name
  // and create a fresh segment under it - a `create` that ran while this one
  // was shutting down. `shm_unlink` works on the name, not on this file, so
  // without the check it would take that newer channel away from its owner.
  bool named_object_is_ours()
  {
    const int probe = shm_open(data_name.c_str(), O_RDWR | O_CLOEXEC, 0666);
    if (probe < 0) {
      return false;
    }
    struct stat mine{}, named{};
    const bool same = fstat(fd, &mine) == 0 && fstat(probe, &named) == 0 &&
      mine.st_dev == named.st_dev && mine.st_ino == named.st_ino;
    ::close(probe);
    return same;
  }
#endif
};

ShmChannel::ShmChannel()
: impl_(std::make_unique<Impl>())
{
}

ShmChannel::~ShmChannel()
{
  if (impl_) {
    impl_->close();
  }
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
  // Windows: the layout header alone. POSIX: the layout header plus the
  // mutex/condvar block the same segment carries. Both are `kHeaderSize`, and
  // the value is stored in the header so an opener maps the ring at the same
  // offset it was written at.
  const uint32_t header_size = static_cast<uint32_t>(kHeaderSize);
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
  // The name is the Windows one with a leading slash, which is what `shm_open`
  // requires - and it goes through `sanitize` for the same reason it does
  // there: a slash inside it would name a file in a directory that does not
  // exist, and an embedded NUL would truncate it.
  const std::string safe = sanitize(name);
  impl.data_name = "/nav2shm_" + safe + "_data";
  if (impl.data_name.size() > 250) {
    // shm_open rejects names longer than NAME_MAX (255 on Linux). Cutting one
    // short would quietly point two different topics at one segment.
    return fail("channel name is too long for a POSIX shared memory object");
  }

  // CreateFileMappingA answers one question on its own: while any process
  // still has a handle open, the name refers to that live object, and once
  // none does - because they closed it, or because they were killed - the
  // kernel destroys it and the next create gets a brand new one. `shm_open`
  // has nothing of the sort in it, and this is the one place where the two
  // platforms have to be made to agree. The liveness is carried by an
  // advisory lock on the segment, which is the piece of state the kernel does
  // take back when a process dies:
  //
  //  * every channel, created or opened, holds it shared for as long as it
  //    exists;
  //  * `create` asks for it exclusively first, and only the absence of every
  //    other holder lets that through. That is the answer to "did somebody
  //    leave this behind", and it is also the permission to re-initialise
  //    what is in there: there is no one left to disturb.
  //
  // What has to be re-initialised is everything a dead run leaves: the ring,
  // which a later `open` would otherwise replay and count from; the mutexes,
  // robust so a dead owner can be recovered - but only by a caller that calls
  // `pthread_mutex_consistent`, and left marked not recoverable if nobody
  // does, which is a state no later lock can ever get out of; and the
  // condition variable, whose waiter counters are the same kind of leftover.
  bool fresh = false;
  impl.fd = shm_open(impl.data_name.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0666);
  if (impl.fd >= 0) {
    // Nobody else can hold a descriptor to a name that did not exist a moment
    // ago, so the exclusive lock below cannot be contended.
    fresh = true;
  } else if (errno == EEXIST) {
    impl.fd = shm_open(impl.data_name.c_str(), O_RDWR | O_CLOEXEC, 0666);
  }
  if (impl.fd < 0) {
    return fail(errno_text("shm_open", errno));
  }
  if (fresh) {
    if (flock(impl.fd, LOCK_EX) != 0) {
      return fail(errno_text("flock", errno));
    }
  } else if (flock(impl.fd, LOCK_EX | LOCK_NB) == 0) {
    // Nothing alive is holding this channel: every byte of it, and every
    // mutex in it, belongs to a run that is over. Take it over and initialise
    // it as if it had just been made.
    fresh = true;
  } else if (errno == EWOULDBLOCK) {
    // A live process still has it open, so the segment is adopted exactly as
    // it stands. The shared lock is taken first: it also waits out the
    // microseconds a `create` that got here first spends holding the
    // exclusive one while it initialises - header, mutexes, ring - which is
    // what keeps this from reading a half-written channel.
    if (flock(impl.fd, LOCK_SH) != 0) {
      return fail(errno_text("flock", errno));
    }
  } else {
    return fail(errno_text("flock", errno));
  }

  struct stat st{};
  if (fstat(impl.fd, &st) != 0) {
    return fail(errno_text("fstat", errno));
  }
  if (fresh) {
    // Nobody else has this mapped - a segment being taken over is held
    // exclusively above - so a leftover that is a different size is nobody
    // else's business and is resized to this channel's layout.
    if (st.st_size != static_cast<off_t>(impl.total_size) &&
      ftruncate(impl.fd, static_cast<off_t>(impl.total_size)) != 0)
    {
      return fail(errno_text("ftruncate", errno));
    }
    st.st_size = static_cast<off_t>(impl.total_size);
  } else if (static_cast<uint64_t>(st.st_size) < impl.total_size) {
    // An adopted segment is never resized: that would pull the ground out
    // from under whoever is reading it.
    return fail("existing shared memory object is smaller than this channel");
  }
  impl.map_size = static_cast<size_t>(st.st_size);

  void * const mapped = mmap(
    nullptr, impl.map_size, PROT_READ | PROT_WRITE, MAP_SHARED, impl.fd, 0);
  if (mapped == MAP_FAILED) {
    return fail(errno_text("mmap", errno));
  }
  impl.base = static_cast<uint8_t *>(mapped);
  impl.header = reinterpret_cast<SharedHeader *>(impl.base);
  impl.sync = reinterpret_cast<ShmSync *>(impl.base + kSyncOffset);

  if (!fresh) {
    if (impl.header->magic != kMagic || impl.header->version != kVersion ||
      impl.header->slot_count != slot_count || impl.header->slot_size != slot_size ||
      impl.header->header_size != header_size)
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

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
    int rc = pthread_mutex_init(&impl.sync->write_mutex, &mutex_attr);
    if (rc == 0) {
      rc = pthread_mutex_init(&impl.sync->wait_mutex, &mutex_attr);
    }
    pthread_mutexattr_destroy(&mutex_attr);

    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    if (rc == 0) {
      rc = pthread_cond_init(&impl.sync->wait_cond, &cond_attr);
    }
    pthread_condattr_destroy(&cond_attr);
    if (rc != 0) {
      return fail(errno_text("process-shared mutex/condvar init", rc));
    }

    for (uint32_t i = 0; i < slot_count; ++i) {
      std::memset(
        impl.base + header_size + static_cast<size_t>(i) * impl.slot_stride, 0,
        sizeof(SlotHeader));
    }
    // The magic comes last, after the sync objects exist: an opener that maps
    // this segment while it is being written sees a header it rejects rather
    // than a mutex nobody initialised. (`write_seq` is already zero from the
    // memset above.)
    impl.header->magic = kMagic;
    impl.header->version = kVersion;
    impl.header->slot_count = slot_count;
    impl.header->slot_size = slot_size;
    impl.header->slot_stride = impl.slot_stride;
    impl.header->header_size = header_size;
    impl.header->write_seq.store(0, std::memory_order_release);
  }
  // Downgrade, and keep it until this channel is closed: from here on the
  // shared lock is what says the segment has a live user, and `close` is what
  // gives it back - or the kernel, if this process never reaches that point.
  if (flock(impl.fd, LOCK_SH) != 0) {
    return fail(errno_text("flock", errno));
  }
  return ch;
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
  const std::string safe = sanitize(name);
  impl.data_name = "/nav2shm_" + safe + "_data";
  if (impl.data_name.size() > 250) {
    return fail("channel name is too long for a POSIX shared memory object");
  }

  impl.fd = shm_open(impl.data_name.c_str(), O_RDWR | O_CLOEXEC, 0666);
  if (impl.fd < 0) {
    return fail(errno == ENOENT ?
      "shm_open failed: channel does not exist" : errno_text("shm_open", errno));
  }
  // Held for as long as this channel exists, and released by the kernel if
  // this process is killed: it is what keeps a `create` elsewhere - a writer
  // that restarted, say - from taking this segment to be abandoned and
  // writing over the ring this reader is following. Taking it also waits out
  // a `create` that is still initialising the segment (see `create`), so
  // nothing below can read a header or a mutex that is half written.
  if (flock(impl.fd, LOCK_SH) != 0) {
    return fail(errno_text("flock", errno));
  }

  struct stat st{};
  if (fstat(impl.fd, &st) != 0) {
    return fail(errno_text("fstat", errno));
  }
  if (static_cast<uint64_t>(st.st_size) < kHeaderSize) {
    return fail("shared memory object is too small to hold a channel");
  }
  impl.map_size = static_cast<size_t>(st.st_size);

  void * const mapped = mmap(
    nullptr, impl.map_size, PROT_READ | PROT_WRITE, MAP_SHARED, impl.fd, 0);
  if (mapped == MAP_FAILED) {
    return fail(errno_text("mmap", errno));
  }
  impl.base = static_cast<uint8_t *>(mapped);
  impl.header = reinterpret_cast<SharedHeader *>(impl.base);
  if (impl.header->magic != kMagic || impl.header->version != kVersion) {
    return fail("invalid channel header");
  }
  impl.slot_count = impl.header->slot_count;
  impl.slot_size = impl.header->slot_size;
  impl.slot_stride = impl.header->slot_stride;
  // A header written by a build whose `ShmSync` is a different size would put
  // the ring at an offset this process no longer agrees with.
  if (impl.header->header_size != static_cast<uint32_t>(kHeaderSize) ||
    impl.slot_count == 0 || impl.slot_stride < sizeof(SlotHeader) + impl.slot_size)
  {
    return fail("invalid channel header");
  }
  impl.total_size = impl.header->header_size +
    static_cast<size_t>(impl.slot_count) * impl.slot_stride;
  if (impl.total_size > impl.map_size) {
    return fail("channel header describes more than the shared memory object holds");
  }
  impl.sync = reinterpret_cast<ShmSync *>(impl.base + kSyncOffset);
  return ch;
#endif
}

bool ShmChannel::publish(const void * data, size_t size)
{
  Impl & impl = *impl_;
  if (!impl.header || size > impl.slot_size) {
    return false;
  }

  // The wait for the writer lock is bounded on purpose. An unbounded one is
  // the one place in the transport that can stop a process forever with no
  // output and no CPU: a holder that never releases - a second run of the same
  // test, a process left over from an earlier one, a tool - owns every publish
  // this process will ever make. On Windows that name is `Local\..._mutex`,
  // which the logon session hands to any process that asks for it; on POSIX it
  // is a mutex in a segment under /dev/shm that the same kind of leftover
  // process can hold the same way. Measured: 161 s of wall clock, 0 s of CPU,
  // stuck inside this call, and nothing in the log.
  //
  // What the wait covers is a memcpy of one slot, so a bound of seconds is
  // generous by orders of magnitude, and a timeout is reported the way every
  // other failure of this function is: `false`. Callers already handle that
  // (`publish_within_deadline` in the tests, the nav2 publisher gate), and a
  // failed publish is something to log; a wedged process is not.
  if (!impl.lock_writer()) {
    return false;
  }
  void * const payload_base = impl.base + impl.header->header_size;
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
  impl.unlock_writer();
  impl.wake_readers();
  return true;
}

bool ShmChannel::subscribe(void * out, size_t capacity, size_t * out_size, int timeout_ms)
{
  Impl & impl = *impl_;
  if (!impl.header) {
    return false;
  }
  std::unique_lock<std::mutex> lock(impl.read_mutex);
  const uint64_t deadline = (timeout_ms < 0) ? 0 :
    mono_ms() + static_cast<uint64_t>(timeout_ms);

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
    if (deadline != 0 && mono_ms() >= deadline) {
      return false;
    }

    impl.prepare_wait();
    if (impl.header->write_seq.load(std::memory_order_acquire) != impl.cursor) {
      continue;
    }
    // Waiting is the one part of a read that blocks, and holding the reader
    // lock through it would stall the thread that is trying to end the wait.
    // The session is re-checked from the top once the lock is back.
    const uint64_t cursor_snapshot = impl.cursor;
    lock.unlock();
    impl.wait_for_wakeup(deadline, cursor_snapshot);
    lock.lock();
  }
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
