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
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
  for (int i = 0; i < 200; ++i) {
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
    std::snprintf(message, sizeof(message), "all 200 rewrites are in the file (%d lost)", lost);
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
    for (int i = 0; i < 200; ++i) {
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
    std::atomic<long> broken{0};
    std::thread poller([&] {
      while (!stop.load()) {
        std::string content;
        ++reads;
        if (!ros2_shim::graph::detail::read_shared(file, content) || content.empty() ||
          content.find("#end") == std::string::npos)
        {
          ++broken;
        }
      }
    });
    for (int i = 0; i < 200; ++i) {
      const std::string churn = "/rewrite_churn_" + std::to_string(i);
      registry.add_service(churn, "std_srvs/srv/Trigger");
      registry.remove_service(churn);
    }
    stop.store(true);
    poller.join();
    char message[200];
    std::snprintf(
      message, sizeof(message), "%ld reads, %ld missing or half written", reads.load(),
      broken.load());
    check(broken.load() == 0, message);
  }

  std::printf("\nwhat a fresh reader sees is what was published last\n");
  {
    registry.add_service("/rewrite_probe_last", "std_srvs/srv/Trigger");
    const std::string content = readPlain(file);
    check(
      content.find("/rewrite_probe_last") != std::string::npos,
      "the newest record is the one that is readable");
    check(
      content.find("/rewrite_churn_199") == std::string::npos,
      "and the records that were removed are gone");
  }

  if (failures != 0) {
    std::printf("\n%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("\nall checks passed\n");
  return 0;
}
