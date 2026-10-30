#ifndef SHM_TRANSPORT_SHM_CHANNEL_HPP
#define SHM_TRANSPORT_SHM_CHANNEL_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace shm_transport
{

// Broadcast shared-memory channel.
//
// One writer publishes variable length byte blobs into a ring of fixed size
// slots. Any number of readers/processes may open the same channel and each
// keeps its own read cursor, so every subscriber observes the full stream.
//
// A slow reader that falls more than `slot_count` messages behind silently
// skips the overwritten messages (counted by dropped_count()).
class ShmChannel
{
public:
  ShmChannel();
  ~ShmChannel();

  ShmChannel(const ShmChannel &) = delete;
  ShmChannel & operator=(const ShmChannel &) = delete;

  // Create the channel. If a live process already has it open, it is adopted
  // as it stands and validated (the layout has to match). A channel left
  // behind by processes that are gone - killed, not closed - is
  // re-initialised instead: the ring, the mutexes and the readers' wakeup
  // state all belonged to those processes, and inheriting them is how a run
  // starts out wedged. Windows gets that distinction from the lifetime of a
  // named kernel object, POSIX from an advisory lock on the segment; see the
  // comment on `Impl::create` in src/shm_transport.cpp.
  static std::unique_ptr<ShmChannel> create(
    const std::string & name, uint32_t slot_count, uint32_t slot_size,
    std::string * error = nullptr);

  // Open an existing channel created by someone else.
  static std::unique_ptr<ShmChannel> open(
    const std::string & name, std::string * error = nullptr);

  // Writer side. Returns false when the payload is larger than slot_size.
  bool publish(const void * data, size_t size);

  // Reader side. Blocks up to timeout_ms (negative = forever, 0 = poll).
  // On success copies up to capacity bytes and stores the payload length in
  // out_size. Returns false on timeout. When capacity is too small the call
  // still advances the cursor and returns false (out_size gets required size).
  bool subscribe(void * out, size_t capacity, size_t * out_size, int timeout_ms);

  // Reader side. Jumps this reader's cursor to the newest published message
  // and returns how many queued messages were skipped, so a reader that joins
  // an existing channel starts at the head of the stream instead of replaying
  // it. The ring keeps its contents after its writer exits, and a brand new
  // subscriber must not be fed those leftovers: ROS 2 volatile durability
  // likewise drops everything published before the subscription existed.
  uint64_t seek_to_latest();

  // Reader side. Positions this reader so its next successful read returns the
  // newest message already in the ring rather than waiting for the next one,
  // and returns how many samples were passed over. ROS 2's `transient_local`
  // durability needs exactly this: a subscription created after the publisher
  // still has to receive the latched sample, the opposite of the volatile case
  // seek_to_latest() implements.
  uint64_t seek_to_latched();

  uint64_t published_count() const;
  uint64_t read_count() const;
  uint64_t dropped_count() const;
  bool is_writer() const;
  const std::string & name() const;
  uint32_t slot_count() const;
  uint32_t slot_size() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace shm_transport

#endif  // SHM_TRANSPORT_SHM_CHANNEL_HPP
