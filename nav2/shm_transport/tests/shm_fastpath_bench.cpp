// What a publish on the shared-memory channel actually costs.
//
// The transport's own test measures the channel as a whole: 64 byte blobs
// through a ring, with the reader and the writer in one process. That is the
// number the documentation quotes, and it says nothing about *where* the time
// goes.
//
// A publish does four things that cost different amounts:
//
//   1. take the writer lock (a mutex in the segment, so a process switch
//      away from it costs a syscall),
//   2. copy the payload into the slot,
//   3. compute the CRC over the payload,
//   4. release the lock and wake the readers.
//
// The CRC is the interesting one: it is a byte-at-a-time table lookup, so it
// costs proportionally to the payload - and a 900 KB camera frame pays it
// twice, once on the way in and once on the way out.
//
// This measures the three separately, on three payload sizes, so the next
// optimization is aimed at the part that is actually big.
//
// Run: nav2/shm_transport/build/shm_fastpath_bench
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "shm_transport/shm_channel.hpp"

namespace
{

// The checksum is the transport's own function - it is exported now, so the
// benchmark measures what the transport really computes rather than a second
// copy of the algorithm that could drift from it.
using shm_transport::crc32c;

double micros_since(std::chrono::steady_clock::time_point start)
{
  return std::chrono::duration<double, std::micro>(
    std::chrono::steady_clock::now() - start).count();
}

}  // namespace

int main()
{
  // Three sizes: the control command, the laser scan, the camera frame.
  const std::size_t sizes[] = {64, 5813, 921600};
  const char * names[] = {"control (64 B)", "laser scan (5.7 KB)", "camera frame (900 KB)"};

  std::printf("=== shared memory: what a publish costs ===\n");
  std::printf("%-24s %10s %10s %10s %10s\n",
              "payload", "copy us", "crc us", "publish us", "crc share");

  for (std::size_t index = 0; index < 3; ++index) {
    const std::size_t size = sizes[index];
    std::vector<std::uint8_t> payload(size, 0xA5);

    // 1. The copy on its own.
    std::vector<std::uint8_t> slot(size);
    const int rounds = size > 100000 ? 200 : 20000;
    volatile std::uint8_t copy_sink = 0;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < rounds; ++i) {
      std::memcpy(slot.data(), payload.data(), size);
      copy_sink = slot.data()[i % size];
    }
    const double copy_us = micros_since(start) / rounds;
    (void)copy_sink;

    // 2. The CRC on its own. The result is folded into a volatile so the
    //    loop cannot be removed: a checksum nobody reads is not a checksum.
    volatile std::uint32_t crc_sink = 0;
    start = std::chrono::steady_clock::now();
    for (int i = 0; i < rounds; ++i) {
      crc_sink = crc32c(payload.data(), size);
    }
    const double crc_us = micros_since(start) / rounds;
    (void)crc_sink;

    // 3. The whole publish: lock, copy, CRC, unlock, wake.
    const std::string name = "shm_fastpath_bench_" + std::to_string(index);
    std::string error;
    auto channel = shm_transport::ShmChannel::create(
      name, 64, static_cast<std::uint32_t>(size + 64), &error);
    if (!channel) {
      std::printf("  %-24s cannot create the channel: %s\n", names[index], error.c_str());
      continue;
    }
    // A reader, so the wake-up has somebody to wake.
    auto reader = shm_transport::ShmChannel::open(name, &error);
    if (!reader) {
      std::printf("  %-24s cannot open the channel: %s\n", names[index], error.c_str());
      continue;
    }
    std::atomic<bool> stop{false};
    std::thread drain([&reader, &stop]() {
        std::vector<std::uint8_t> buffer(reader->slot_size());
        while (!stop.load()) {
          std::size_t read = 0;
          reader->subscribe(buffer.data(), buffer.size(), &read, 0);
        }
      });

    // Warm the path up, then measure.
    for (int i = 0; i < 50; ++i) {
      channel->publish(payload.data(), payload.size());
    }
    start = std::chrono::steady_clock::now();
    for (int i = 0; i < rounds; ++i) {
      channel->publish(payload.data(), payload.size());
    }
    const double publish_us = micros_since(start) / rounds;
    stop.store(true);
    drain.join();

    std::printf(
      "%-24s %10.3f %10.3f %10.3f %9.1f%%\n", names[index], copy_us, crc_us, publish_us,
      publish_us > 0 ? (copy_us + crc_us) * 100.0 / publish_us : 0.0);
    if (publish_us > 10.0 * (copy_us + crc_us) && publish_us > 0) {
      std::printf(
        "  %-24s the publish waited for the reader: the ring filled up because the drain\n"
        "  %-24s thread cannot keep pace at %.0f msg/s. The lock and the wake-up are\n"
        "  %-24s what the rest of the cost is.\n",
        "", "", 1e6 / publish_us, "");
    }
  }
  return 0;
}
