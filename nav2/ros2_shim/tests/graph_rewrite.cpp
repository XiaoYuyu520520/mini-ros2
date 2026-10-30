// A registry rewrite has to land even while somebody is reading the graph.
//
// Every shim process keeps its nodes, topics, services and actions in one file
// per process (`<pid>.graph`) and rewrites it on every change. On Windows that
// rewrite cannot happen under a reader's nose the way it can on POSIX: a file
// another process holds open refuses to be replaced, and the refusal is quiet.
// What a dropped rewrite costs is not a stale listing - it is a node that is
// running, with its services served and its topics published, that no other
// process can see: the lifecycle manager reports `get_state service is not
// available!` for a service that is right there, and a publisher that gates
// `/plan` on "somebody is subscribed" reads a graph that says nobody is.
//
// So this pins down both halves of the contract, against the real file format
// and the real registry:
//
//   1. a rewrite lands while this process holds the file open for reading - the
//      case the shim's own readers create,
//   2. a rewrite waits out a reader that will not share the file, the way an
//      indexer or an older build reads it,
//   3. a client polling `service_exists()` never loses sight of a service that
//      was registered the whole time,
//   4. a reader never sees the graph blank, truncated or half written, and what
//      a fresh reader sees is the state that was published last.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "ros2_shim/graph.hpp"

namespace
{

int failures = 0;

void check(bool ok, const std::string & what)
{
  std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) {
    ++failures;
  }
}

#if defined(_WIN32)
// The names of the failures that matter here, spelled out: a run that fails
// should say which one it was without anybody having to look a number up.
const char * error_name(int code)
{
  switch (code) {
    case 0: return "nothing recorded";
    case 2: return "ERROR_FILE_NOT_FOUND";
    case 3: return "ERROR_PATH_NOT_FOUND";
    case 5: return "ERROR_ACCESS_DENIED";
    case 32: return "ERROR_SHARING_VIOLATION";
    default: return "unrecognized";
  }
}
#else
const char * error_name(int)
{
  return "not recorded on this platform";
}
#endif

std::string readPlain(const std::filesystem::path & path)

{
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The graph file, held open across a rewrite the way a reader holds it: a
// `collect()` in another process is between its open and its close for as long
// as the swap takes. The sharing mode is the whole point of the exercise and
// `std::ifstream` cannot ask for one, so the handle is opened by hand.
class HeldReader
{
public:
  explicit HeldReader(const std::filesystem::path & path)
#if defined(_WIN32)
  : win_handle_(CreateFileW(
      path.wstring().c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr))
  {
  }
#else
  : stream_(path, std::ios::binary)
  {
  }
#endif

  ~HeldReader()
  {
    close();
  }

  HeldReader(const HeldReader &) = delete;
  HeldReader & operator=(const HeldReader &) = delete;

  bool isOpen() const
  {
#if defined(_WIN32)
    return win_handle_ != INVALID_HANDLE_VALUE;
#else
    return stream_.is_open();
#endif
  }

  void close()
  {
#if defined(_WIN32)
    if (win_handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(win_handle_);
      win_handle_ = INVALID_HANDLE_VALUE;
    }
#else
    stream_.close();
#endif
  }

private:
#if defined(_WIN32)
  HANDLE win_handle_;
#else
  std::ifstream stream_;
#endif
};

}  // namespace

int main()
{
  namespace fs = std::filesystem;
  // A graph directory this test owns: the registry is per machine, and the
  // records of everything else running must not end up in what is read here.
  const fs::path dir = fs::temp_directory_path() / "ros2_shim_graph_rewrite_test";
  fs::remove_all(dir);
#if defined(_WIN32)
  _putenv_s("NAV2_SHM_GRAPH_DIR", dir.string().c_str());
#else
  setenv("NAV2_SHM_GRAPH_DIR", dir.string().c_str(), 1);
#endif

  auto & registry = ros2_shim::graph::ProcessRegistry::instance();
  registry.add_service("/rewrite_probe", "std_srvs/srv/Trigger");
  const fs::path file = registry.file_path();
  check(fs::exists(file), "the registry wrote its file");

  std::printf("\na rewrite lands while a reader is holding the graph\n");
  int lost = 0;
  // Sixty rewrites, each with a reader holding the file across it: that is the
  // expensive shape (the swap that tolerates a live reader is the slow one) and
  // sixty of them is already far more contention than a real graph sees.
  for (int i = 0; i < 30; ++i) {
    const std::string name = "/rewrite_probe_" + std::to_string(i);
    HeldReader holding(file);
    if (!holding.isOpen()) {
      ++lost;
      continue;
    }
    registry.add_service(name, "std_srvs/srv/Trigger");
    holding.close();
    if (readPlain(file).find(name) == std::string::npos) {
      ++lost;
    }
  }
  {
    char message[160];
    std::snprintf(message, sizeof(message), "all 30 rewrites are in the file (%d lost)", lost);
    check(lost == 0, message);
  }

  std::printf("\na rewrite waits out a reader that will not share the file\n");
  {
    // A tool that has no idea this file exists - an indexer, a virus scanner,
    // an `ifstream` held by an older build - refuses to share it, and a rewrite
    // cannot land while it is there. It is there for a moment, so the rewrite
    // has to still be trying when it goes away.
    const std::string name = "/rewrite_after_a_greedy_reader";
    std::ifstream greedy(file, std::ios::binary);
    check(greedy.is_open(), "the greedy reader has the file");
    std::thread writer([&registry, &name] {
      registry.add_service(name, "std_srvs/srv/Trigger");
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    greedy.close();
    writer.join();
    check(
      readPlain(file).find(name) != std::string::npos,
      "the rewrite landed once the reader let go");
  }

  std::printf("\na client polling the graph keeps sight of a service that is there\n");
  {
    std::atomic<bool> stop{false};
    std::atomic<long> polls{0};
    std::atomic<long> missed{0};
    std::thread poller([&] {
      while (!stop.load()) {
        ++polls;
        if (!ros2_shim::graph::service_exists("/rewrite_probe")) {
          ++missed;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });
    for (int i = 0; i < 60; ++i) {
      const std::string churn = "/rewrite_churn_" + std::to_string(i);
      registry.add_service(churn, "std_srvs/srv/Trigger");
      registry.remove_service(churn);
    }
    stop.store(true);
    poller.join();
    char message[200];
    std::snprintf(
      message, sizeof(message), "%ld polls, %ld saw the service missing", polls.load(),
      missed.load());
    check(missed.load() == 0, message);
  }

  std::printf("\na reader never sees a blank or half written graph\n");
  {
    std::atomic<bool> stop{false};
    std::atomic<long> reads{0};
    // Three ways for a read to come back wrong, counted apart (measured: only
    // the first ever happens - see 03-validation.md, evidence 42). They are
    // three different bugs in three different places - the swap lost the name, the
    // name pointed at a zero length file, or the bytes were there without the
    // `#end` marker - and "1 missing or half written" out of 1400 does not say
    // which. Whoever picks this up next should not have to guess, and a failure
    // that names its own shape is worth more than one that only counts.
    std::atomic<long> unreadable{0};
    std::atomic<long> empty_file{0};
    std::atomic<long> unterminated{0};
    // And for the reads that did not open at all, *why*. `read_shared` hands
    // back the `GetLastError()` of the last `CreateFileW` it tried, because
    // "the name was away for a moment" and "somebody else is holding the file
    // and will not share it" are two different problems with two different
    // answers, and the error code is the only thing that tells them apart.
    std::mutex codes_mutex;
    std::map<int, long> codes;
    std::thread poller([&] {
      while (!stop.load()) {
        std::string content;
        int code = 0;
        ++reads;
        if (!ros2_shim::graph::detail::read_shared(file, content, &code)) {
          ++unreadable;
          std::lock_guard<std::mutex> held(codes_mutex);
          ++codes[code];
        } else if (content.empty()) {
          ++empty_file;
        } else if (content.find("#end") == std::string::npos) {
          ++unterminated;
        }
      }
    });
    for (int i = 0; i < 60; ++i) {
      const std::string churn = "/rewrite_churn_" + std::to_string(i);
      registry.add_service(churn, "std_srvs/srv/Trigger");
      registry.remove_service(churn);
    }
    stop.store(true);
    poller.join();
    char message[400];
    const long broken = unreadable.load() + empty_file.load() + unterminated.load();
    std::string why;
    {
      std::lock_guard<std::mutex> held(codes_mutex);
      for (const auto & entry : codes) {
        why += " | ";
        why += std::to_string(entry.second);
        why += "x ";
        why += error_name(entry.first);
      }
    }
    std::snprintf(
      message, sizeof(message),
      "%ld reads, %ld missing or half written (%ld not openable, %ld empty, "
      "%ld without #end)%s", reads.load(), broken, unreadable.load(), empty_file.load(),
      unterminated.load(), why.c_str());
    check(broken == 0, message);
  }

  std::printf("\nwhat a fresh reader sees is what was published last\n");
  {
    registry.add_service("/rewrite_probe_last", "std_srvs/srv/Trigger");
    const std::string content = readPlain(file);
    check(
      content.find("/rewrite_probe_last") != std::string::npos,
      "the newest record is the one that is readable");
    check(
      content.find("/rewrite_churn_59") == std::string::npos,
      "and the records that were removed are gone");
  }

  // Leave nothing behind, in the directory the registry *really* used.
  //
  // `graph_dir()` appends `NAV2_SHM_NAMESPACE`, and ctest sets that variable for
  // every test in this tree (`CMakeLists.txt`, the loop at the end), so the
  // `remove_all(dir)` at the top of this file cleans a directory nobody wrote
  // to. Measured: three of these were sitting in `%TEMP%` from earlier runs
  // (`..._test_ctest_ros2_shim_graph_rewrite`, `..._test_audit_...`,
  // `..._test_gr_probe`). Asking the registry where its file is avoids having a
  // second copy of that rule to keep in step.
  {
    std::error_code ignored;
    std::filesystem::remove_all(file.parent_path(), ignored);
    if (file.parent_path() != dir) {
      std::filesystem::remove_all(dir, ignored);
    }
  }

  if (failures != 0) {
    std::printf("\n%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("\nall checks passed\n");
  return 0;
}
