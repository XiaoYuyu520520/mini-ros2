// The other half of the rewrite contract: the reader is a second process.
//
// `graph_rewrite.cpp` pins the swap down against handles this process holds.
// What it cannot pin down is the case this file exists for: somebody else -
// an indexer, a virus scanner, a build from before `read_shared` learned to ask
// for delete access - has `<pid>.graph` open in a way the swap cannot get past.
// The rewrite is refused for as long as that lasts. What must not happen is
// that the refusal outlives the reader, because the writer's next change may
// never come: a node that registered its services once and then only spins has
// no reason to rewrite the file, and every other process answers questions from
// that file. A record lost that way is a service that is served and cannot be
// seen, which is what nav2's lifecycle manager reports as `get_state service is
// not available!` and what makes a publisher gating on "somebody is subscribed"
// decide that nobody is.
//
// So a child process holds the file and then lets go, and this asserts:
//
//   1. the holder really did block the swap (Windows only - a POSIX rename is
//      never refused by a reader, so there is nothing to block there),
//   2. the records written while it was held reach the file afterwards, with
//      the parent changing nothing else - what lands is the retry,
//   3. what lands is complete and well formed, not just the last record.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "ros2_shim/graph.hpp"

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

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

std::string readPlain(const fs::path & path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Path of this executable, so the holder can be this same binary.
fs::path selfPath()
{
#if defined(_WIN32)
  std::vector<wchar_t> buffer(1024);
  for (;;) {
    const DWORD got = GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (got == 0) {
      return {};
    }
    if (got < buffer.size() - 1) {
      return fs::path(std::wstring(buffer.data(), got));
    }
    buffer.resize(buffer.size() * 2);
  }
#else
  std::vector<char> buffer(1024);
  for (;;) {
    const ssize_t got = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (got <= 0) {
      return {};
    }
    if (static_cast<std::size_t>(got) < buffer.size()) {
      return fs::path(std::string(buffer.data(), static_cast<std::size_t>(got)));
    }
    buffer.resize(buffer.size() * 2);
  }
#endif
}

// The child: holds `file` open for `ms`, announcing itself through `marker`.
//
// The sharing mode is the whole point and `std::ifstream` cannot ask for one, so
// the handle is opened by hand - without `FILE_SHARE_DELETE`, which is the flag
// a rewrite needs and the one a reader that has never heard of this file does
// not give it.
int hold(const fs::path & file, const fs::path & marker, int ms)
{
#if defined(_WIN32)
  HANDLE handle = CreateFileW(
    file.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL, nullptr);
  const bool opened = handle != INVALID_HANDLE_VALUE;
#else
  const int handle = ::open(file.string().c_str(), O_RDONLY);
  const bool opened = handle >= 0;
#endif
  bool announced = false;
  {
    // Where the marker goes is the parent's business, but making the directory
    // here too is what turns a mistaken path into "announced, and it says
    // failed" instead of a holder that looks like it never started at all.
    std::error_code ignored;
    std::filesystem::create_directories(marker.parent_path(), ignored);
    std::ofstream announce(marker, std::ios::binary | std::ios::trunc);
    announce << (opened ? "open" : "failed") << "\n";
    announced = static_cast<bool>(announce);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#if defined(_WIN32)
  if (opened) {
    CloseHandle(handle);
  }
#else
  if (opened) {
    ::close(handle);
  }
#endif
  return (opened && announced) ? 0 : 1;
}

// The holder process, from the parent's side.
class Holder
{
public:
  ~Holder()
  {
#if defined(_WIN32)
    if (running_) {
      CloseHandle(process_.hThread);
      CloseHandle(process_.hProcess);
    }
#endif
  }

  bool start(const fs::path & file, const fs::path & marker, int ms)
  {
    marker_ = marker;
    const fs::path self = selfPath();
    if (self.empty()) {
      return false;
    }
#if defined(_WIN32)
    std::wstring command = L"\"" + self.wstring() + L"\" --hold \"" + file.wstring() +
                           L"\" \"" + marker.wstring() + L"\" " + std::to_wstring(ms);
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(
        nullptr, buffer.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
        &process_))
    {
      return false;
    }
    running_ = true;
    return true;
#else
    const std::string self_string = self.string();
    const std::string ms_string = std::to_string(ms);
    pid_ = ::fork();
    if (pid_ < 0) {
      return false;
    }
    if (pid_ == 0) {
      ::execl(
        self_string.c_str(), self_string.c_str(), "--hold", file.string().c_str(),
        marker.string().c_str(), ms_string.c_str(), static_cast<char *>(nullptr));
      ::_exit(127);
    }
    return true;
#endif
  }

  // Waits for the child to say it has the file open, so the writes below are
  // guaranteed to be racing a holder rather than a process that is still
  // starting up.
  bool waitForMarker(int timeout_ms)
  {
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      if (fs::exists(marker_)) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
  }

  // The holder's exit code if it is already gone, -1 while it is still running.
  //
  // Only used to explain a holder that never announced itself: "exited with
  // code 1" and "still running" are different bugs, and a silent failure here
  // costs whoever picks this up an afternoon.
  int exited()
  {
#if defined(_WIN32)
    if (!running_ || WaitForSingleObject(process_.hProcess, 0) != WAIT_OBJECT_0) {
      return -1;
    }
    DWORD code = 0;
    GetExitCodeProcess(process_.hProcess, &code);
    return static_cast<int>(code);
#else
    if (pid_ <= 0) {
      return -1;
    }
    int status = 0;
    if (::waitpid(pid_, &status, WNOHANG) != pid_) {
      return -1;
    }
    pid_ = -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
  }

  void wait()
  {
#if defined(_WIN32)
    if (running_) {
      WaitForSingleObject(process_.hProcess, INFINITE);
      CloseHandle(process_.hThread);
      CloseHandle(process_.hProcess);
      running_ = false;
    }
#else
    if (pid_ > 0) {
      int status = 0;
      ::waitpid(pid_, &status, 0);
      pid_ = -1;
    }
#endif
  }

private:
  fs::path marker_;
#if defined(_WIN32)
  PROCESS_INFORMATION process_{};
  bool running_ = false;
#else
  pid_t pid_ = -1;
#endif
};

// Long enough that the writer's own retry budget (50 ms per `install_file`)
// expires several times while the file is out of reach.
const int kHoldMs = 800;

}  // namespace

int main(int argc, char ** argv)
{
  if (argc >= 5 && std::string(argv[1]) == "--hold") {
    return hold(fs::path(argv[2]), fs::path(argv[3]), std::atoi(argv[4]));
  }

  // A graph directory this test owns, the way `graph_rewrite.cpp` does it: the
  // records of everything else running must not end up in what is read here.
  const fs::path base = fs::temp_directory_path() / "ros2_shim_graph_cross_process_test";
  fs::remove_all(base);
#if defined(_WIN32)
  _putenv_s("NAV2_SHM_GRAPH_DIR", base.string().c_str());
#else
  setenv("NAV2_SHM_GRAPH_DIR", base.string().c_str(), 1);
#endif

  auto & registry = ros2_shim::graph::ProcessRegistry::instance();
  registry.add_service("/cross_process_probe", "std_srvs/srv/Trigger");
  const fs::path file = registry.file_path();
  // The directory the registry *really* used, asked of the registry instead of
  // recomputed: `graph_dir()` appends `NAV2_SHM_NAMESPACE`, and ctest sets that
  // variable for every test in this tree (`CMakeLists.txt`, the loop at the
  // end), so `base` is not where the file is when this runs under ctest. The
  // hand-built copy of that rule is what this test got wrong first: the holder
  // announced itself into a directory nobody had created, and the run looked
  // like a holder that never started.
  const fs::path dir = file.parent_path();
  check(fs::exists(file), "the registry wrote its file");

  std::printf("\na second process holds the graph file while the registry rewrites it\n");
  Holder holder;
  if (!holder.start(file, dir / "holder_ready", kHoldMs)) {
    check(false, "the holder process started");
    std::printf("\n%d check(s) failed\n", failures);
    return 1;
  }
  const bool announced = holder.waitForMarker(5000);
  if (!announced) {
    const int code = holder.exited();
    if (code >= 0) {
      std::printf("       the holder exited with code %d without announcing itself\n", code);
    } else {
      std::printf("       the holder is still running but never announced itself\n");
    }
  }
  check(announced, "the holder has the file open");

  std::vector<std::string> written;
  for (int i = 0; i < 5; ++i) {
    const std::string name = "/cross_process_" + std::to_string(i);
    registry.add_service(name, "std_srvs/srv/Trigger");
    written.push_back(name);
  }

#if defined(_WIN32)
  {
    // The writes above cannot have landed: the holder refuses to share delete
    // access, so `MoveFileEx`/`ReplaceFileW` both fail and `install_file` gives
    // up. This is the failure the retry exists for, and asserting it is what
    // separates "the retry works" from "there was never anything to retry".
    const std::string held = readPlain(file);
    check(
      held.find(written.back()) == std::string::npos,
      "the swap was refused while the holder had the file");
  }
#else
  // A POSIX rename is atomic and is never refused because of a reader, so there
  // is no window here for the holder to create; what is checked below is the
  // same invariant, and it holds trivially.
  std::printf("[skip] a reader cannot refuse a rename on this platform\n");
#endif

  holder.wait();

  // From here the parent changes nothing: whatever lands is the retry, not
  // another write.
  bool landed = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    if (readPlain(file).find(written.back()) != std::string::npos) {
      landed = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  check(
    landed,
    "the refused rewrite lands after the holder lets go, with no further change");

  const std::string content = readPlain(file);
  int missing = 0;
  for (const auto & name : written) {
    if (content.find(name) == std::string::npos) {
      ++missing;
    }
  }
  char message[160];
  std::snprintf(
    message, sizeof(message), "all %d records written while it was held are in the file (%d missing)",
    static_cast<int>(written.size()), missing);
  check(missing == 0, message);
  check(
    content.find("#end") != std::string::npos,
    "and the file that landed is complete, not half written");
  check(content.find("/cross_process_probe") != std::string::npos, "the first record is still there");

  // Leave nothing behind. The directory is this test's own - the namespace
  // suffix included - and `%TEMP%` growing by one graph directory per run is
  // the kind of thing nobody notices until it is 58 MB.
  {
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    if (dir != base) {
      fs::remove_all(base, ignored);
    }
  }

  if (failures != 0) {
    std::printf("\n%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("\nall checks passed\n");
  return 0;
}
