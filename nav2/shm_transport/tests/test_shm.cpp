#include "shm_transport/shm_channel.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace shm_transport;

namespace
{

int g_failures = 0;

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::printf("  FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
      ++g_failures; \
    } \
  } while (0)

uint64_t now_ms()
{
  return static_cast<uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
// Channel names live in the logon session (`Local\`), and this build is not
// the only `test_shm.exe` on the machine: `bridge\windows\build` links the
// same library. Two of them would share every channel and one would block on
// the other's publish mutex, so the name carries the same
// `NAV2_SHM_NAMESPACE` the shim puts on its channels - which CTest sets per
// test (nav2/ros2_shim/CMakeLists.txt). Without it, the names are global.
std::string channel_name(const char * case_name)
{
  const char * const ns = std::getenv("NAV2_SHM_NAMESPACE");
  if (ns == nullptr || *ns == '\0') {
    return case_name;
  }
  return std::string(ns) + "_" + case_name;
}

// Payload layout: [uint64 index][uint32 size][random bytes...]
std::vector<uint8_t> make_message(uint64_t index, uint32_t size, std::mt19937_64 & rng)
{
  std::vector<uint8_t> buf(size);
  std::memcpy(buf.data(), &index, sizeof(index));
  const uint32_t s = size;
  std::memcpy(buf.data() + sizeof(index), &s, sizeof(s));
  for (size_t i = sizeof(index) + sizeof(s); i < buf.size(); ++i) {
    buf[i] = static_cast<uint8_t>(rng() & 0xFFU);
  }
  return buf;
}

// Publish, with a deadline.
//
// The ring overwrites its oldest slot rather than refusing when it is full, so
// the one way `publish` returns false is the one way retrying cannot fix: a
// payload larger than a slot, or a channel whose header never came up. A
// `while (!publish(...))` around that is an infinite loop on a condition that
// is permanently false, and what the machine does then is hang a test that has
// already failed - inside a `consumer.join()` that also never returns, with no
// output saying which call it was. It hung once for minutes for exactly this
// reason. The loop is still a retry (a genuinely transient false would be
// covered), but it is bounded, and it says what it was doing when it gave up.
bool publish_within_deadline(
  ShmChannel & channel, const void * data, size_t size, uint64_t budget_ms, const char * what)
{
  const uint64_t deadline = now_ms() + budget_ms;
  while (!channel.publish(data, size)) {
    if (now_ms() >= deadline) {
      std::printf(
        "  FAIL: %s: publish of %zu bytes never succeeded in %llums\n",
        what, size, static_cast<unsigned long long>(budget_ms));
      ++g_failures;
      return false;
    }
    std::this_thread::yield();
  }
  return true;
}

bool verify_message(const std::vector<uint8_t> & buf, uint64_t expected_index)
{
  if (buf.size() < sizeof(uint64_t) + sizeof(uint32_t)) {
    return false;
  }
  uint64_t index = 0;
  std::memcpy(&index, buf.data(), sizeof(index));
  return index == expected_index;
}

void test_broadcast_threads()
{
  std::printf("[test] broadcast: one writer, two readers\n");
  const uint32_t slots = 256;
  const uint32_t slot_size = 1024;
  const uint64_t total = 20000;
  const std::string name = channel_name("test_broadcast");

  auto writer = ShmChannel::create(name, slots, slot_size);
  CHECK(writer != nullptr);
  auto reader_a = ShmChannel::open(name);
  auto reader_b = ShmChannel::open(name);
  CHECK(reader_a != nullptr && reader_b != nullptr);
  if (!writer || !reader_a || !reader_b) {
    return;
  }

  auto consume = [](ShmChannel * reader, uint64_t total) -> uint64_t {
      std::vector<uint8_t> buf(4096);
      uint64_t next = 0;
      while (next < total) {
        size_t len = 0;
        if (reader->subscribe(buf.data(), buf.size(), &len, 5000)) {
          uint64_t index = 0;
          std::memcpy(&index, buf.data(), sizeof(index));
          if (index >= next) {
            next = index + 1;
          }
        }
      }
      return next;
    };

  uint64_t count_a = 0;
  uint64_t count_b = 0;
  std::thread ta([&]() {count_a = consume(reader_a.get(), total);});
  std::thread tb([&]() {count_b = consume(reader_b.get(), total);});

  std::mt19937_64 rng(12345);
  uint64_t sent = 0;
  while (sent < total) {
    const uint32_t size = 64 + static_cast<uint32_t>(rng() % 512);
    auto msg = make_message(sent, size, rng);
    if (writer->publish(msg.data(), msg.size())) {
      ++sent;
    } else {
      std::this_thread::yield();
    }
  }

  ta.join();
  tb.join();

  CHECK(count_a == total);
  CHECK(count_b == total);
  CHECK(writer->published_count() == total);
  CHECK(reader_a->dropped_count() == 0 || count_a == total);
  std::printf(
    "  published=%llu readerA=%llu readerB=%llu droppedA=%llu droppedB=%llu\n",
    static_cast<unsigned long long>(writer->published_count()),
    static_cast<unsigned long long>(count_a),
    static_cast<unsigned long long>(count_b),
    static_cast<unsigned long long>(reader_a->dropped_count()),
    static_cast<unsigned long long>(reader_b->dropped_count()));
}

void test_drop_on_slow_reader()
{
  std::printf("[test] slow reader drops oldest messages\n");
  const uint32_t slots = 8;
  const uint32_t slot_size = 256;
  const std::string name = channel_name("test_drop");

  auto writer = ShmChannel::create(name, slots, slot_size);
  auto reader = ShmChannel::open(name);
  CHECK(writer != nullptr && reader != nullptr);
  if (!writer || !reader) {
    return;
  }

  std::mt19937_64 rng(7);
  for (uint64_t i = 0; i < 100; ++i) {
    auto msg = make_message(i, 128, rng);
    CHECK(writer->publish(msg.data(), msg.size()));
  }

  uint64_t received = 0;
  std::vector<uint8_t> buf(512);
  size_t len = 0;
  while (reader->subscribe(buf.data(), buf.size(), &len, 0)) {
    ++received;
  }
  CHECK(received == slots);
  CHECK(reader->dropped_count() == 100 - slots);
  std::printf(
    "  received=%llu dropped=%llu\n",
    static_cast<unsigned long long>(received),
    static_cast<unsigned long long>(reader->dropped_count()));
}

void test_timeout()
{
  std::printf("[test] subscribe timeout on empty channel\n");
  const std::string name = channel_name("test_timeout");
  auto writer = ShmChannel::create(name, 4, 128);
  auto reader = ShmChannel::open(name);
  CHECK(writer != nullptr && reader != nullptr);
  if (!writer || !reader) {
    return;
  }
  uint8_t out[128];
  size_t len = 0;
  const uint64_t t0 = now_ms();
  const bool got = reader->subscribe(out, sizeof(out), &len, 200);
  const uint64_t elapsed = now_ms() - t0;
  CHECK(!got);
  CHECK(elapsed >= 150 && elapsed < 2000);
  std::printf("  elapsed=%llums (expected ~200)\n", static_cast<unsigned long long>(elapsed));

  std::mt19937_64 rng(1);
  auto msg = make_message(0, 100, rng);
  CHECK(writer->publish(msg.data(), msg.size()));
  CHECK(reader->subscribe(out, sizeof(out), &len, 1000));
  CHECK(len == msg.size());
}

void test_throughput()
{
  std::printf("[test] throughput (64B messages, 1 writer/1 reader)\n");
  const std::string name = channel_name("test_throughput");
  auto writer = ShmChannel::create(name, 4096, 256);
  auto reader = ShmChannel::open(name);
  CHECK(writer != nullptr && reader != nullptr);
  if (!writer || !reader) {
    return;
  }

  const uint64_t total = 1000000;
  // `stop` is what makes a failure on the writer side a failure here rather
  // than a join that never returns: without it the consumer keeps waiting for
  // messages that are never coming, and the test hangs instead of reporting.
  std::atomic<bool> stop{false};
  std::atomic<bool> done{false};
  std::atomic<uint64_t> received{0};
  std::vector<uint8_t> buf(256);
  std::thread consumer([&]() {
      while (!stop.load() && received.load() < total) {
        size_t len = 0;
        if (reader->subscribe(buf.data(), buf.size(), &len, 1000)) {
          received.fetch_add(1);
        }
      }
      done.store(true);
    });

  const uint64_t t0 = now_ms();
  std::mt19937_64 rng(99);
  auto msg = make_message(0, 64, rng);
  bool every_publish_landed = true;
  for (uint64_t i = 0; i < total; ++i) {
    std::memcpy(msg.data(), &i, sizeof(i));
    if (!publish_within_deadline(*writer, msg.data(), msg.size(), 5000, "throughput")) {
      every_publish_landed = false;
      stop.store(true);
      break;
    }
  }
  const uint64_t sent_ms = now_ms() - t0;
  consumer.join();
  const uint64_t total_ms = now_ms() - t0;

  const double sent_rate = total * 1000.0 / (sent_ms ? sent_ms : 1);
  const double full_rate = total * 1000.0 / (total_ms ? total_ms : 1);
  std::printf(
    "  sent=%llu in %llums (%.0f msg/s), consumer done in %llums (%.0f msg/s), received=%llu\n",
    static_cast<unsigned long long>(total), static_cast<unsigned long long>(sent_ms),
    sent_rate, static_cast<unsigned long long>(total_ms), full_rate,
    static_cast<unsigned long long>(received.load()));
  CHECK(received.load() == total);
  CHECK(every_publish_landed);
}

// ---- cross process test -------------------------------------------------

std::string exe_path()
{
#ifdef _WIN32
  char buf[MAX_PATH] = {0};
  GetModuleFileNameA(nullptr, buf, MAX_PATH);
  return std::string(buf);
#else
  // CTest runs the test with the build directory as the working directory, but
  // the children are exec'd by path, and a relative one would follow whoever
  // launched the test. /proc/self/exe is the image that is running.
  char buf[4096] = {0};
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    return std::string(buf, static_cast<size_t>(n));
  }
  return std::string("./test_shm");
#endif
}

int run_writer_process(const std::string & name, uint64_t total)
{
  auto writer = ShmChannel::create(name, 1024, 1024);
  if (!writer) {
    std::printf("child writer: create failed\n");
    return 2;
  }
  std::mt19937_64 rng(2024);
  for (uint64_t i = 0; i < total; ++i) {
    auto msg = make_message(i, 200 + static_cast<uint32_t>(i % 300), rng);
    if (!publish_within_deadline(*writer, msg.data(), msg.size(), 5000, "child writer")) {
      // The reader in the other process is waiting for these; leaving without
      // them turns one failure into two, and the exit code says why.
      return 3;
    }
  }
  return 0;
}

int run_reader_process(const std::string & name, uint64_t total)
{
  auto reader = ShmChannel::open(name);
  if (!reader) {
    std::printf("child reader: open failed\n");
    return 2;
  }
  std::vector<uint8_t> buf(2048);
  uint64_t next = 0;
  uint64_t received = 0;
  const uint64_t deadline = now_ms() + 30000;
  while (next < total && now_ms() < deadline) {
    size_t len = 0;
    if (reader->subscribe(buf.data(), buf.size(), &len, 500)) {
      uint64_t index = 0;
      std::memcpy(&index, buf.data(), sizeof(index));
      if (index >= next) {
        next = index + 1;
      }
      ++received;
    }
  }
  std::printf("child reader: received %llu messages, caught up to %llu/%llu dropped=%llu\n",
    static_cast<unsigned long long>(received), static_cast<unsigned long long>(next),
    static_cast<unsigned long long>(total),
    static_cast<unsigned long long>(reader->dropped_count()));
  return next == total ? 0 : 4;
}

void test_cross_process()
{
  std::printf("[test] cross-process writer + reader\n");
  const std::string name = channel_name("test_xproc");
  const uint64_t total = 50000;
  const uint32_t slots = 1024;
  const uint32_t slot_size = 1024;

#ifdef _WIN32
  // Parent keeps a handle open so the named objects survive writer exit.
  auto keeper = ShmChannel::create(name, slots, slot_size);
  CHECK(keeper != nullptr);
  if (!keeper) {
    return;
  }

  const std::string exe = exe_path();
  auto spawn = [&](const std::string & role) -> HANDLE {
      std::string cmd = "\"" + exe + "\" " + role + " " + name + " " + std::to_string(total);
      std::vector<char> mutable_cmd(cmd.begin(), cmd.end());
      mutable_cmd.push_back('\0');
      STARTUPINFOA si{};
      si.cb = sizeof(si);
      PROCESS_INFORMATION pi{};
      if (!CreateProcessA(
          nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
      {
        std::printf("  CreateProcess(%s) failed: %lu\n", role.c_str(), GetLastError());
        return nullptr;
      }
      CloseHandle(pi.hThread);
      return pi.hProcess;
    };

  HANDLE writer = spawn("child-writer");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  HANDLE reader = spawn("child-reader");
  if (!writer || !reader) {
    CHECK(false);
    return;
  }
  DWORD writer_code = 1;
  DWORD reader_code = 1;
  const DWORD wr = WaitForSingleObject(writer, 40000);
  const DWORD rr = WaitForSingleObject(reader, 40000);
  GetExitCodeProcess(writer, &writer_code);
  GetExitCodeProcess(reader, &reader_code);
  CloseHandle(writer);
  CloseHandle(reader);
  CHECK(wr == WAIT_OBJECT_0 && writer_code == 0);
  CHECK(rr == WAIT_OBJECT_0 && reader_code == 0);
  std::printf("  writer exit=%lu reader exit=%lu\n", writer_code, reader_code);
#else
  // The same shape as the Windows branch: the parent holds a channel open so
  // that the segment outlives the children. The POSIX side unlinks a channel
  // when its last user goes away, and without a keeper the writer's exit could
  // take the segment out from under the reader.
  auto keeper = ShmChannel::create(name, slots, slot_size);
  CHECK(keeper != nullptr);
  if (!keeper) {
    return;
  }

  const std::string exe = exe_path();
  const std::string total_text = std::to_string(total);
  auto spawn = [&](const std::string & role) -> pid_t {
      const pid_t pid = fork();
      if (pid < 0) {
        std::printf("  fork(%s) failed: %s\n", role.c_str(), std::strerror(errno));
        return -1;
      }
      if (pid == 0) {
        // Between fork and exec only async-signal-safe calls, which is why the
        // strings above are built before the fork.
        char * const argv[] = {
          const_cast<char *>(exe.c_str()), const_cast<char *>(role.c_str()),
          const_cast<char *>(name.c_str()), const_cast<char *>(total_text.c_str()), nullptr};
        execv(exe.c_str(), argv);
        std::printf("  execv(%s) failed: %s\n", exe.c_str(), std::strerror(errno));
        _exit(127);
      }
      return pid;
    };
  auto wait_for = [](pid_t pid) -> int {
      int status = 0;
      if (pid < 0 || waitpid(pid, &status, 0) < 0) {
        return -1;
      }
      return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    };

  const pid_t writer = spawn("child-writer");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const pid_t reader = spawn("child-reader");
  const int writer_code = wait_for(writer);
  const int reader_code = wait_for(reader);
  CHECK(writer_code == 0);
  CHECK(reader_code == 0);
  std::printf("  writer exit=%d reader exit=%d\n", writer_code, reader_code);
#endif
}

#ifndef _WIN32
// ---- channels left behind by a writer that is gone ----------------------
//
// A Windows named object is destroyed by the kernel when the last handle to it
// closes, and a killed process closes its handles like any other one. A POSIX
// segment is not: it sits in /dev/shm until somebody unlinks it. `create` is
// where the two have to be made to agree, so these two cases are the whole of
// that contract - the name of a writer that is gone comes up empty, and the
// name of one that is alive comes up exactly as that writer left it.
//
// It is not a hypothetical: a segment whose writer was killed was adopted by
// the next run, `wake_readers` met a `wait_mutex` whose owner had died and
// walked away from it without releasing it, and the first `publish` of that
// run waited for that mutex forever - 0% CPU, nothing in the log, the whole
// test suite timed out at the throughput case above this one. Every run after
// it inherited the same corpse and hung in the same place.

// Child side of both cases. Publishes `count` messages, then either leaves
// without running a destructor - what a killed process leaves, because `_exit`
// skips the unlink - or tells the parent it is ready and waits to be released,
// so that the parent is the one that has to deal with a writer that is alive.
int run_child_writer(const std::string & name, uint64_t count, int ready_write, int release_read)
{
  auto writer = ShmChannel::create(name, 64, 256);
  if (!writer) {
    return 2;
  }
  std::mt19937_64 rng(4242);
  for (uint64_t i = 0; i < count; ++i) {
    auto msg = make_message(i, 128, rng);
    if (!publish_within_deadline(*writer, msg.data(), msg.size(), 2000, "child writer")) {
      return 3;
    }
  }
  if (ready_write >= 0) {
    const char byte = 'r';
    const bool wrote = (::write(ready_write, &byte, 1) == 1);
    ::close(ready_write);
    if (!wrote) {
      return 4;
    }
  }
  if (release_read >= 0) {
    char byte = 0;
    const bool read_one = (::read(release_read, &byte, 1) == 1);
    ::close(release_read);
    if (!read_one) {
      return 5;
    }
  }
  return 0;
}

void test_abandoned_channel()
{
  std::printf("[test] a channel whose writer is gone comes up empty\n");
  const std::string name = channel_name("test_orphan");
  const pid_t pid = fork();
  if (pid == 0) {
    // `_exit`, so that neither the channel's destructor nor the interpreter's
    // exit path unlinks the segment: the process stops, the segment stays.
    _exit(run_child_writer(name, 5, -1, -1));
  }
  int status = 0;
  CHECK(pid > 0 && waitpid(pid, &status, 0) == pid);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

  auto writer = ShmChannel::create(name, 64, 256);
  CHECK(writer != nullptr);
  if (!writer) {
    return;
  }
  // A new ring, not the dead writer's: the count below is what a later run
  // would have started from had the leftovers been inherited.
  CHECK(writer->published_count() == 0);
  auto reader = ShmChannel::open(name);
  CHECK(reader != nullptr);
  if (!reader) {
    return;
  }
  CHECK(reader->published_count() == 0);

  std::mt19937_64 rng(11);
  auto msg = make_message(0, 128, rng);
  CHECK(publish_within_deadline(*writer, msg.data(), msg.size(), 2000, "after the orphan"));
  std::vector<uint8_t> buf(256);
  size_t len = 0;
  CHECK(reader->subscribe(buf.data(), buf.size(), &len, 2000));
  CHECK(verify_message(buf, 0));
  std::printf("  published after the orphan=%llu\n",
    static_cast<unsigned long long>(writer->published_count()));
}

void test_live_writer_is_not_clobbered()
{
  std::printf("[test] a channel a live writer holds is adopted, not reset\n");
  const std::string name = channel_name("test_live");
  int ready[2] = {-1, -1};
  int release[2] = {-1, -1};
  CHECK(pipe(ready) == 0 && pipe(release) == 0);
  if (ready[0] < 0 || release[0] < 0) {
    return;
  }

  const pid_t pid = fork();
  if (pid == 0) {
    ::close(ready[0]);
    ::close(release[1]);
    _exit(run_child_writer(name, 5, ready[1], release[0]));
  }
  ::close(ready[1]);
  ::close(release[0]);

  char byte = 0;
  CHECK(pid > 0 && ::read(ready[0], &byte, 1) == 1);
  ::close(ready[0]);

  // The writer is alive and has published five messages. Whatever this call
  // does, it may not take the ring away from it.
  auto writer = ShmChannel::create(name, 64, 256);
  CHECK(writer != nullptr);
  if (writer) {
    CHECK(writer->published_count() == 5);
  }

  const char go = 'g';
  CHECK(::write(release[1], &go, 1) == 1);
  ::close(release[1]);
  int status = 0;
  CHECK(waitpid(pid, &status, 0) == pid);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

  // The writer that died was not the last user, so the segment outlived it -
  // the name goes away with the last channel, not the first.
  if (writer) {
    CHECK(writer->published_count() == 5);
    auto reader = ShmChannel::open(name);
    CHECK(reader != nullptr);
    if (reader) {
      reader->seek_to_latest();
      std::mt19937_64 rng(3);
      auto msg = make_message(5, 128, rng);
      CHECK(publish_within_deadline(*writer, msg.data(), msg.size(), 2000, "after the writer left"));
      std::vector<uint8_t> buf(256);
      size_t len = 0;
      CHECK(reader->subscribe(buf.data(), buf.size(), &len, 2000));
      CHECK(verify_message(buf, 5));
      std::printf("  published=%llu, and the ring outlived the writer\n",
        static_cast<unsigned long long>(writer->published_count()));
    }
  }
}
#endif  // !_WIN32

}  // namespace

int main(int argc, char ** argv)
{
  if (argc >= 4 && std::strcmp(argv[1], "child-writer") == 0) {
    return run_writer_process(argv[2], std::strtoull(argv[3], nullptr, 10));
  }
  if (argc >= 4 && std::strcmp(argv[1], "child-reader") == 0) {
    return run_reader_process(argv[2], std::strtoull(argv[3], nullptr, 10));
  }

  std::printf("=== shm_transport tests ===\n");
  test_timeout();
  test_drop_on_slow_reader();
  test_broadcast_threads();
  test_throughput();
  test_cross_process();
#ifndef _WIN32
  test_abandoned_channel();
  test_live_writer_is_not_clobbered();
#endif

  if (g_failures == 0) {
    std::printf("=== ALL TESTS PASSED ===\n");
    return 0;
  }
  std::printf("=== %d CHECK(S) FAILED ===\n", g_failures);
  return 1;
}
