#ifndef ROS2_SHIM_GRAPH_HPP_
#define ROS2_SHIM_GRAPH_HPP_

// Cross-process ROS2-like graph registry for the shared-memory shim.
//
// The shim has no DDS discovery: nodes only meet through named shared-memory
// channels (see rclcpp/shm_config.hpp). Tooling such as `ros2 topic list`
// therefore needs a place where every process advertises the nodes and topics
// it currently owns.
//
// Design: every process writes one small text file into a well known directory
// (`NAV2_SHM_GRAPH_DIR`, default `<temp>/nav2_shim_graph`) named after its pid.
// Readers merge all files, skip files that are still being written (missing the
// trailing `#end` marker) and delete files whose owner has exited. No locking is
// needed because a process only ever rewrites its own file.
//
// The directory carries `NAV2_SHM_NAMESPACE` as a suffix, the way channel names
// do (`<temp>/nav2_shim_graph_<NAMESPACE>`, see rclcpp/shm_config.hpp). The two
// have to move together or the namespace means half of what it says: a process
// in another namespace still shows up in this one's `ros2 topic list`, and - the
// part that is not cosmetic - `rclcpp::Client::service_is_ready()` answers `true`
// for a service whose *channels* are in that other namespace, so a caller sends
// a request into a ring nobody is reading and waits for an answer that cannot
// come. Separating the directory as well is what makes "a process started
// without the namespace cannot collide with one that was" true of the graph and
// not only of the shared memory.
//
// File format (tab separated, UTF-8), one record per line:
//   #nav2shim-graph\t1
//   node\t<fqn>\t<source>
//   topic\t<name>\t<type>\t<publishers>\t<subscribers>\t<source>
//   service\t<name>\t<type>\t<source>
//   action\t<name>\t<type>\t<source>
//   #end

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <errno.h>
#  include <signal.h>
#  include <unistd.h>
#endif

namespace ros2_shim
{
namespace graph
{

struct TopicEntry
{
  std::string name;
  std::string type;
  int publishers = 0;
  int subscribers = 0;
  // "local" for topics owned by a process on this machine, otherwise the peer
  // that mirrors the topic in, e.g. "remote:192.168.1.113:50051".
  std::string source = "local";
};

struct NodeEntry
{
  std::string name;
  std::string source = "local";
};

// An advertised service. The shim has no discovery of its own, so this is what
// `rclcpp::Client::wait_for_service()` reads: a client can only tell that
// `/<node>/change_state` exists because its server put a record here.
struct ServiceEntry
{
  std::string name;
  std::string type;
  // "local" for services served by a process on this machine, otherwise the
  // peer that mirrors them in, the same convention topics use.
  std::string source = "local";
};

// An advertised action server, e.g. `compute_path_to_pose`. Upstream answers
// `rclcpp_action::Client::wait_for_action_server()` from DDS discovery of the
// three services every action is built on; here it is this record, written by
// `rclcpp_action::Server`'s constructor (and by the gRPC bridge for an action
// the robot serves), that says a goal can be sent.
struct ActionEntry
{
  std::string name;
  std::string type;
  std::string source = "local";
};

// Action names are recorded fully qualified - `compute_path_to_pose`, which is
// how nav2 spells its action servers, is stored and listed as
// `/compute_path_to_pose`, the way ROS2 tools print it.
//
// Note that this is the *record's* spelling. The six shared-memory channels of
// an action are built from the name the caller passed (`name_ + "/_goal"`,
// resolved against the node's namespace by `Node::get_channel`), which is what
// a client and a server have to agree on to talk; the registry only has to
// agree with itself, and it does, because every writer and every reader goes
// through this function.
inline std::string qualified_action_name(const std::string & name)
{
  if (name.empty() || name[0] == '/') {
    return name;
  }
  return "/" + name;
}

namespace detail
{

inline std::string env_string(const char * name)
{
  const char * value = std::getenv(name);
  return value ? std::string(value) : std::string();
}

inline std::filesystem::path temp_directory()
{
  for (const char * name : {"TMPDIR", "TEMP", "TMP"}) {
    const std::string value = env_string(name);
    if (!value.empty()) {
      return std::filesystem::path(value);
    }
  }
  return std::filesystem::temp_directory_path();
}

inline std::filesystem::path graph_dir()
{
  const std::string override_dir = env_string("NAV2_SHM_GRAPH_DIR");
  const std::filesystem::path base = override_dir.empty() ?
    temp_directory() / "nav2_shim_graph" : std::filesystem::path(override_dir);
  // The same prefix `channel_name()` puts in front of a topic, so one variable
  // separates both halves of a stack. An empty namespace - the default - appends
  // nothing and leaves every existing path alone.
  const std::string name_space = env_string("NAV2_SHM_NAMESPACE");
  if (name_space.empty()) {
    return base;
  }
  return base.string() + "_" + name_space;
}

inline unsigned long current_pid()
{
#if defined(_WIN32)
  return static_cast<unsigned long>(GetCurrentProcessId());
#else
  return static_cast<unsigned long>(getpid());
#endif
}

// The bytes of a graph file, read the way a writer can live with.
//
// On Windows the reader decides whether the file it is holding can be replaced:
// an `std::ifstream` opens without sharing delete access, and a rewrite that
// collides with it does not land at all - measured against this file, 15% of
// rewrites were lost under a reader that read in a loop, quietly, with the old
// contents left in place. Opening through `CreateFileW` with `FILE_SHARE_DELETE`
// is what lets the rewrite go ahead: this reader keeps the bytes it opened, the
// name points at the new ones.
//
// The retry covers the other direction, and it has to cover *every* failure,
// `ERROR_FILE_NOT_FOUND` included: the swap below takes the name away for the
// moment between moving the old file aside and moving the new one in (measured
// against this file: 52 of 956 opens landed in that window, and every one of
// them was a moment of the file being gone, not of it never having been there).
// Reading "not found" as "nothing to read" is how a registered service
// disappears from a graph that is holding it.
//
// The patience here is deliberately longer than the writer's whole retry budget
// (`install_file` gives up after 50 ms): while a writer is still trying, the
// file can be locked or momentarily gone, and the one answer a reader must
// never give is "this node serves nothing" about a node that is serving. A
// caller may be made to wait; it may not be told something untrue. The wait
// only ever happens while a swap is in flight, and the swap is microseconds
// when the readers are the shim's own.
// Whether a caller that just lost a race with the other side of a swap should
// try again, after stepping aside for a moment.
//
// The window being waited out is microseconds wide - the gap between one
// process taking a file's name away and putting the next one in its place - so
// the first tries only yield. Sleeping a millisecond on each of them is what
// turns a registry write that costs nothing into one that costs a millisecond,
// and a reader that polls in a loop then spends its time asleep: measured, a
// writer and a reader trading the same file went from ~30 ms per update to
// under a millisecond once the waits were yields. The sleep is kept for the
// last stretch, where the other side is a reader that is simply slow to let go
// and spinning on it would burn a core for nothing.
inline bool retry_swap(
  const std::chrono::steady_clock::time_point & start, int attempt, int attempts,
  const std::chrono::milliseconds & budget)
{
  if (attempt + 1 >= attempts) {
    return false;
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  if (elapsed >= budget) {
    return false;
  }
  if (elapsed < std::chrono::milliseconds(1)) {
    std::this_thread::yield();
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

// `last_error` is a diagnostic, filled in on Windows only and never used by the
// production paths: when a read gives up it holds the `GetLastError()` of the
// last `CreateFileW` that failed. The three shapes of a failed read are three
// different bugs - `ERROR_FILE_NOT_FOUND` is a swap in flight that outlasted the
// patience below, `ERROR_ACCESS_DENIED` / `ERROR_SHARING_VIOLATION` is somebody
// else holding the file - and a caller that only knows "it did not open" cannot
// tell them apart.
inline bool read_shared(
  const std::filesystem::path & path, std::string & content, int * last_error = nullptr)
{
  content.clear();
  if (last_error != nullptr) {
    *last_error = 0;
  }
#if defined(_WIN32)
  const auto start = std::chrono::steady_clock::now();
  for (int attempt = 0; attempt < 20000; ++attempt) {
    HANDLE handle = CreateFileW(
      path.wstring().c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      // Read before anything else: `GetLastError` is per thread and the wait
      // below is a syscall that would overwrite it.
      const DWORD failure = GetLastError();
      // Two shapes of "not openable", and they want opposite things from a
      // reader. A name that is not there is a swap in flight - microseconds -
      // or a record that has been removed; waiting a long time for it only
      // makes a poll loop slower, so that one keeps the short patience. A name
      // that is there but held is another process in the middle of a swap, and
      // under `ctest -j 8` that window outlasted 60 ms about once in 1500 reads
      // (04-gaps.md, section 7). The miss is silent: `collect` skips the file,
      // and the caller then reads a graph with that process missing from it -
      // which is what nav2's subscriber gate reads. A second is ~16 times the
      // widest window measured here and is still a bound, so a read cannot
      // hang on a file that nobody ever lets go of.
      const bool held = failure != ERROR_FILE_NOT_FOUND && failure != ERROR_PATH_NOT_FOUND;
      const std::chrono::milliseconds patience = held ?
        std::chrono::milliseconds(1000) : std::chrono::milliseconds(60);
      if (!retry_swap(start, attempt, 20000, patience)) {
        if (last_error != nullptr) {
          *last_error = static_cast<int>(failure);
        }
        return false;
      }
      continue;
    }
    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(handle, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
      content.append(buffer, static_cast<std::size_t>(got));
    }
    CloseHandle(handle);
    return true;
  }
  return false;
#else
  // A POSIX rename is atomic and is never refused because of a reader, so
  // there is no window to wait out.
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
#endif
}

// Puts the freshly written `from` in place of `to`, whatever readers are doing.
//
// `std::filesystem::rename` is not enough here, and its failure is quiet: on
// Windows replacing a file another process holds open fails with
// `ERROR_ACCESS_DENIED`, and if the old file was removed first then the graph
// has no file at all - a node that is running, with its services served and its
// topics published, that no other process can see. `ReplaceFileW` is the API
// for swapping a file with a reader in the way, `MoveFileEx` covers the first
// write (there is nothing to replace yet) and the filesystems that refuse a
// replace, and the retry covers the moment when even those lose the race with a
// reader that will not share the file.
//
// `MoveFileEx` is tried first because it is the one that never leaves the name
// unreadable: it either swaps the entry or fails, while `ReplaceFileW` moves
// the old file aside and back, and a reader that lands in between is told the
// file is not there for a moment (see `read_shared`). So the swap with the
// window is the fallback for the case that has no other answer - somebody else
// is holding the file.
inline bool install_file(const std::string & from, const std::string & to)
{
#if defined(_WIN32)
  const std::wstring source = std::filesystem::path(from).wstring();
  const std::wstring target = std::filesystem::path(to).wstring();
  const auto start = std::chrono::steady_clock::now();
  for (int attempt = 0; attempt < 20000; ++attempt) {
    if (MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
      return true;
    }
    if (ReplaceFileW(
        target.c_str(), source.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS,
        nullptr, nullptr))
    {
      return true;
    }
    if (MoveFileExW(source.c_str(), target.c_str(), 0)) {
      return true;  // nothing was there to replace
    }
    if (!retry_swap(start, attempt, 2000, std::chrono::milliseconds(50))) {
      return false;
    }
  }
  return false;
#else
  std::error_code error;
  std::filesystem::rename(from, to, error);
  return !error;
#endif
}

// Whether a pid is *certainly* not running any more.
//
// Only a definite answer is good enough to delete somebody else's graph file.
// `OpenProcess` also fails when the machine is out of handles or a filter
// driver refuses the query, and reading that as "the owner is gone" destroys a
// live node's records - it does not rewrite that file until its own graph
// changes again, so the node disappears from `ros2 node list`, from
// `wait_for_service()` and from the subscriber counts a publisher gates on,
// with nothing to explain why.
inline bool process_gone(unsigned long pid)
{
  if (pid == 0) {
    return false;
  }
#if defined(_WIN32)
  HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
  if (handle == nullptr) {
    return GetLastError() == ERROR_INVALID_PARAMETER;  // no such process
  }
  DWORD code = 0;
  const bool alive = GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE;
  CloseHandle(handle);
  return !alive;
#else
  return !(kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM);
#endif
}

// Fields are tab separated, so tabs and newlines inside values are replaced.
inline std::string sanitize(const std::string & value)
{
  std::string out = value;
  for (char & c : out) {
    if (c == '\t' || c == '\n' || c == '\r') {
      c = ' ';
    }
  }
  return out;
}

inline std::vector<std::string> split_tab(const std::string & line)
{
  std::vector<std::string> fields;
  std::string current;
  for (char c : line) {
    if (c == '\t') {
      fields.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  fields.push_back(current);
  return fields;
}

inline unsigned long parse_pid(const std::filesystem::path & path)
{
  const std::string stem = path.stem().string();
  std::size_t end = 0;
  unsigned long pid = 0;
  try {
    pid = std::stoul(stem, &end);
  } catch (...) {
    return 0;
  }
  return end == stem.size() ? pid : 0;
}

}  // namespace detail

// Per-process view of the graph. Every node registers itself and its topics
// here, and the registry rewrites this process' graph file on every change.
class ProcessRegistry
{
public:
  static ProcessRegistry & instance()
  {
    static ProcessRegistry registry;
    existing_slot() = &registry;
    return registry;
  }

  // The registry of this process, if it has one - `nullptr` otherwise.
  //
  // `collect` asks for this process' own records instead of reading its own
  // file (see the note there). A process that only reads the graph - the
  // `ros2` CLI - must not be handed one: creating it would register this
  // process as a node, which is a change nobody asked for.
  static ProcessRegistry * existing()
  {
    return existing_slot();
  }

  // Copies this process' records into a merged view; see the definition.
  void merge_own(
    std::vector<TopicEntry> & topics, std::vector<NodeEntry> & nodes,
    std::vector<ServiceEntry> * services, std::vector<ActionEntry> * actions);

  ProcessRegistry(const ProcessRegistry &) = delete;
  ProcessRegistry & operator=(const ProcessRegistry &) = delete;

  void add_node(const std::string & name, const std::string & source = "local")
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto & entry : nodes_) {
      if (entry.name == name) {
        entry.source = source;
        flush_locked();
        return;
      }
    }
    nodes_.push_back(NodeEntry{name, source});
    flush_locked();
  }

  void remove_node(const std::string & name)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    nodes_.erase(
      std::remove_if(
        nodes_.begin(), nodes_.end(),
        [&](const NodeEntry & entry) {return entry.name == name;}),
      nodes_.end());
    flush_locked();
  }

  // Registers an advertised topic. `role` is "pub" or "sub".
  void add_topic(
    const std::string & topic, const std::string & type, const std::string & role,
    const std::string & source = "local")
  {
    std::lock_guard<std::mutex> lock(mutex_);
    TopicEntry * entry = find_locked(topic, source);
    if (!entry) {
      topics_.push_back(TopicEntry{topic, type, 0, 0, source});
      entry = &topics_.back();
    }
    // The type on the record is the publisher's: a topic has one type, and a
    // record that let a subscription overwrite it would answer `ros2 topic
    // info` with whatever happened to be created last in this process.
    if (!type.empty() && (role == "pub" || entry->type.empty())) {
      entry->type = type;
    }
    if (role == "pub") {
      ++entry->publishers;
    } else if (role == "sub") {
      ++entry->subscribers;
    }
    flush_locked();
  }

  void remove_topic(
    const std::string & topic, const std::string & role = "",
    const std::string & source = "local")
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = topics_.begin(); it != topics_.end(); ) {
      if (it->name != topic || it->source != source) {
        ++it;
        continue;
      }
      if (role == "pub") {
        it->publishers = it->publishers > 0 ? it->publishers - 1 : 0;
      } else if (role == "sub") {
        it->subscribers = it->subscribers > 0 ? it->subscribers - 1 : 0;
      } else {
        it->publishers = 0;
        it->subscribers = 0;
      }
      if (it->publishers == 0 && it->subscribers == 0) {
        it = topics_.erase(it);
      } else {
        ++it;
      }
    }
    flush_locked();
  }

  // -------------------------------------------------------------- services
  // Registers a service this process serves. Called from `rclcpp::Service`'s
  // constructor, so every service on the node - the ones nav2 servers create,
  // and the `~/change_state` family rclcpp_lifecycle makes - is advertised.
  void add_service(
    const std::string & service, const std::string & type = "",
    const std::string & source = "local")
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ServiceEntry * entry = find_service_locked(service, source);
    if (!entry) {
      services_.push_back(ServiceEntry{service, type, source});
      flush_locked();
      return;
    }
    if (!type.empty()) {
      entry->type = type;
    }
    flush_locked();
  }

  // Withdrawn when the service is destroyed: a node that drops a service (a
  // lifecycle node cleaning up, a server shutting down) must stop looking
  // ready to every client waiting on it.
  void remove_service(const std::string & service, const std::string & source = "local")
  {
    std::lock_guard<std::mutex> lock(mutex_);
    services_.erase(
      std::remove_if(
        services_.begin(), services_.end(),
        [&](const ServiceEntry & entry) {
          return entry.name == service && entry.source == source;
        }),
      services_.end());
    flush_locked();
  }

  std::vector<ServiceEntry> local_services() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return services_;
  }

  // ---------------------------------------------------------------- actions
  // The action counterpart of add_service: called from `rclcpp_action::Server`'s
  // constructor, so every action server in this process - nav2's
  // `compute_path_to_pose` and `follow_path`, the behaviors - can be found by a
  // client that only knows the name. Without it `wait_for_action_server()`
  // would have nothing to look at.
  void add_action(
    const std::string & name, const std::string & type = "",
    const std::string & source = "local")
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string action = qualified_action_name(name);
    ActionEntry * entry = find_action_locked(action, source);
    if (!entry) {
      actions_.push_back(ActionEntry{action, type, source});
      flush_locked();
      return;
    }
    if (!type.empty()) {
      entry->type = type;
    }
    flush_locked();
  }

  void remove_action(const std::string & name, const std::string & source = "local")
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string action = qualified_action_name(name);
    actions_.erase(
      std::remove_if(
        actions_.begin(), actions_.end(),
        [&](const ActionEntry & entry) {
          return entry.name == action && entry.source == source;
        }),
      actions_.end());
    flush_locked();
  }

  std::vector<ActionEntry> local_actions() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return actions_;
  }

  // A topic this process mirrors in from another machine (see the gRPC bridge).
  void set_remote_topic(
    const std::string & topic, const std::string & type, const std::string & source)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    TopicEntry * entry = find_locked(topic, source);
    if (!entry) {
      topics_.push_back(TopicEntry{topic, type, 1, 0, source});
      flush_locked();
      return;
    }
    entry->type = type;
    entry->publishers = 1;
    flush_locked();
  }

  void clear_topics_from_source(const std::string & source)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    topics_.erase(
      std::remove_if(
        topics_.begin(), topics_.end(),
        [&](const TopicEntry & entry) {return entry.source == source;}),
      topics_.end());
    flush_locked();
  }

  // The service counterpart: the bridge withdraws every service it mirrored in
  // when the connection to the robot goes away, so a client's
  // `wait_for_service()` stops reporting a service nobody can answer any more.
  void clear_services_from_source(const std::string & source)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    services_.erase(
      std::remove_if(
        services_.begin(), services_.end(),
        [&](const ServiceEntry & entry) {return entry.source == source;}),
      services_.end());
    flush_locked();
  }

  // And the action counterpart: an action the bridge mirrored in must stop
  // looking sendable when the robot becomes unreachable.
  void clear_actions_from_source(const std::string & source)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    actions_.erase(
      std::remove_if(
        actions_.begin(), actions_.end(),
        [&](const ActionEntry & entry) {return entry.source == source;}),
      actions_.end());
    flush_locked();
  }

  std::vector<TopicEntry> local_topics() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return topics_;
  }

  std::string file_path() const
  {
    return (detail::graph_dir() / (std::to_string(detail::current_pid()) + ".graph")).string();
  }

  // Rewrites this process' graph file (done automatically on every change).
  void flush()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    flush_locked();
  }

private:
  // A rewrite that lost its swap is retried here instead of in the caller.
  //
  // `detail::install_file` gives up after 50 ms, and losing that race used to
  // mean the record was gone: the file kept its previous contents, and a node
  // whose graph does not change again - a server that registered its services
  // once and then only spins - never rewrote it. Every other process answers
  // questions from that file, so what is lost is a service that is served and
  // cannot be seen, which is how this shows up as `get_state service is not
  // available!` about a service that is right there, or as a publisher that
  // gates on "somebody is subscribed" deciding that nobody is.
  //
  // The retry cannot be a second `install_file` on the same name - a failed
  // `ReplaceFileW` can take the `.tmp` with it - so the whole write is redone,
  // off the caller's thread. It costs nothing while the first swap lands.
  ProcessRegistry()
  {
    reflush_ = std::thread([this] {reflush_loop();});
  }

  static ProcessRegistry * & existing_slot()
  {
    static ProcessRegistry * slot = nullptr;
    return slot;
  }

  ~ProcessRegistry()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    reflush_cv_.notify_all();
    if (reflush_.joinable()) {
      reflush_.join();
    }
    // Only once the thread is gone: a rewrite that landed after this would put
    // the file back, and leave the graph advertising a process that is no
    // longer running.
    std::error_code error;
    std::filesystem::remove(file_path(), error);
    // Then the directory itself. `remove` on a directory only succeeds
    // while it is empty, which is exactly the right test: empty means this
    // was the last process in this namespace, and a namespace with anyone
    // else in it (or a `<pid>.graph` left by a process that was killed)
    // keeps its directory and says no. Without this every namespace a test
    // run ever picked stayed behind for good - one empty
    // `%TEMP%\nav2_shim_graph_<namespace>` each, 37 of them by the time it
    // was noticed. A process that needs the directory again recreates it in
    // `write_and_install_locked`, which is also what heals the race where a
    // sibling is between `create_directories` and its own swap.
    std::filesystem::remove(detail::graph_dir(), error);
  }

  TopicEntry * find_locked(const std::string & topic, const std::string & source)
  {
    for (auto & entry : topics_) {
      if (entry.name == topic && entry.source == source) {
        return &entry;
      }
    }
    return nullptr;
  }

  ServiceEntry * find_service_locked(const std::string & service, const std::string & source)
  {
    for (auto & entry : services_) {
      if (entry.name == service && entry.source == source) {
        return &entry;
      }
    }
    return nullptr;
  }

  ActionEntry * find_action_locked(const std::string & action, const std::string & source)
  {
    for (auto & entry : actions_) {
      if (entry.name == action && entry.source == source) {
        return &entry;
      }
    }
    return nullptr;
  }

  // Writes the records to `<pid>.graph.tmp` and swaps it in.
  //
  // Split out of `flush_locked` because a swap that lost has to be retried as a
  // whole - see `ProcessRegistry()` - and "as a whole" means writing a fresh
  // `.tmp` first: `install_file` may have moved the previous one away before it
  // gave up, so handing the same name to it again is not a retry.
  bool write_and_install_locked()
  {
    std::error_code error;
    std::filesystem::create_directories(detail::graph_dir(), error);
    const std::string final_path = file_path();
    const std::string temp_path = final_path + ".tmp";

    std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out << "#nav2shim-graph\t1\n";
    for (const auto & node : nodes_) {
      out << "node\t" << detail::sanitize(node.name) << '\t'
          << detail::sanitize(node.source) << '\n';
    }
    for (const auto & topic : topics_) {
      out << "topic\t" << detail::sanitize(topic.name) << '\t'
          << detail::sanitize(topic.type) << '\t' << topic.publishers << '\t'
          << topic.subscribers << '\t' << detail::sanitize(topic.source) << '\n';
    }
    for (const auto & service : services_) {
      out << "service\t" << detail::sanitize(service.name) << '\t'
          << detail::sanitize(service.type) << '\t' << detail::sanitize(service.source) << '\n';
    }
    for (const auto & action : actions_) {
      out << "action\t" << detail::sanitize(action.name) << '\t'
          << detail::sanitize(action.type) << '\t' << detail::sanitize(action.source) << '\n';
    }
    out << "#end\n";
    out.close();

    // No `remove` before the rename: dropping the old file first leaves the
    // graph with no file at all when the rename then loses a race with a
    // reader, and a missing file is not "an old graph", it is "this process
    // serves nothing". The replace keeps the old contents readable until the
    // new ones are in place, and waits out a reader that is holding it.
    return detail::install_file(temp_path, final_path);
  }

  void flush_locked()
  {
    if (write_and_install_locked()) {
      dirty_ = false;
      return;
    }
    // The swap lost. Hand the state to the thread rather than trying again
    // here: the caller is a node in the middle of registering something, and
    // waiting out a reader that may hold the file for a second is not its job.
    dirty_ = true;
    reflush_cv_.notify_all();
  }

  // Keeps rewriting this process' file until a swap lands.
  //
  // Waiting rather than spinning is the point: the other side is a reader that
  // is slow to let go, not work to be done. A failed attempt already spent up
  // to `install_file`'s 50 ms budget against it, so the pause is short and the
  // loop is quiet - and it only exists at all while a write is outstanding.
  void reflush_loop()
  {
    const std::chrono::milliseconds pause(10);
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      reflush_cv_.wait(lock, [this] {return stop_ || dirty_;});
      if (stop_) {
        return;
      }
      if (write_and_install_locked()) {
        dirty_ = false;
        continue;
      }
      reflush_cv_.wait_for(lock, pause, [this] {return stop_;});
      if (stop_) {
        return;
      }
    }
  }

  // Set when the file on disk is known to be behind these records. Read and
  // written under `mutex_`, like the records themselves.
  bool dirty_ = false;
  bool stop_ = false;
  std::thread reflush_;
  std::condition_variable reflush_cv_;

  mutable std::mutex mutex_;
  std::vector<NodeEntry> nodes_;
  std::vector<TopicEntry> topics_;
  std::vector<ServiceEntry> services_;
  std::vector<ActionEntry> actions_;
};

// ------------------------------------------------------------------ reading

inline void merge_topic(std::vector<TopicEntry> & topics, const TopicEntry & incoming)
{
  for (auto & entry : topics) {
    if (entry.name != incoming.name || entry.source != incoming.source) {
      continue;
    }
    // Same rule across processes: the type comes from a record that publishes
    // the topic, so a merged view is not decided by file read order. A record
    // with subscribers only keeps a name for a topic nobody publishes yet.
    if (entry.type.empty() || (incoming.publishers > 0 && entry.publishers == 0)) {
      entry.type = incoming.type;
    }
    entry.publishers += incoming.publishers;
    entry.subscribers += incoming.subscribers;
    return;
  }
  topics.push_back(incoming);
}

// Two processes never serve the same service name on the same source, so a
// service is merged on (name, source) rather than summed the way topics are.
inline void merge_service(
  std::vector<ServiceEntry> & services, const ServiceEntry & incoming)
{
  for (auto & entry : services) {
    if (entry.name != incoming.name || entry.source != incoming.source) {
      continue;
    }
    if (entry.type.empty()) {
      entry.type = incoming.type;
    }
    return;
  }
  services.push_back(incoming);
}

// Actions merge on (name, source) for the same reason services do.
inline void merge_action(
  std::vector<ActionEntry> & actions, const ActionEntry & incoming)
{
  for (auto & entry : actions) {
    if (entry.name != incoming.name || entry.source != incoming.source) {
      continue;
    }
    if (entry.type.empty()) {
      entry.type = incoming.type;
    }
    return;
  }
  actions.push_back(incoming);
}

// This process' own records, straight out of memory.
//
// `collect` reads them from here rather than from this process' own file. That
// file is a copy of exactly these records, kept for the *other* processes to
// read, and a refused swap leaves it a generation behind - or, when the swap is
// refused outright, leaves that registration out of it altogether. Reading the
// file would make this process' own view depend on that write succeeding, and
// that is how a publisher ends up counting its subscribers as zero and stops
// publishing (04-gaps.md section 7 item 2; the deterministic reproducer is
// `.scratch\_partial_graph_hold.py`).
inline void ProcessRegistry::merge_own(
  std::vector<TopicEntry> & topics, std::vector<NodeEntry> & nodes,
  std::vector<ServiceEntry> * services, std::vector<ActionEntry> * actions)
{
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto & entry : topics_) {
    merge_topic(topics, entry);
  }
  nodes.insert(nodes.end(), nodes_.begin(), nodes_.end());
  if (services != nullptr) {
    for (const auto & entry : services_) {
      merge_service(*services, entry);
    }
  }
  if (actions != nullptr) {
    for (const auto & entry : actions_) {
      merge_action(*actions, entry);
    }
  }
}

// `services` and `actions` are optional: the callers that only want topics or
// nodes pass nothing and those records are then simply skipped.
inline void collect(
  std::vector<TopicEntry> & topics, std::vector<NodeEntry> & nodes, bool remove_stale,
  std::vector<ServiceEntry> * services = nullptr,
  std::vector<ActionEntry> * actions = nullptr)
{
  std::error_code error;
  const std::filesystem::path dir = detail::graph_dir();
  if (!std::filesystem::exists(dir, error)) {
    return;
  }

  for (const auto & item : std::filesystem::directory_iterator(dir, error)) {
    if (error) {
      break;
    }
    if (!item.is_regular_file(error) || item.path().extension() != ".graph") {
      continue;
    }

    const unsigned long pid = detail::parse_pid(item.path());
    if (remove_stale && detail::process_gone(pid)) {
      std::error_code ignored;
      std::filesystem::remove(item.path(), ignored);
      continue;
    }
    // This process' own file is a copy of records that are already here, and a
    // copy is only as good as its last rewrite. Ask the registry instead: it
    // cannot be stale, and a refused swap can no longer make this process
    // invisible to its own queries. The other processes still read the file -
    // writing it is what that is for.
    if (pid == detail::current_pid()) {
      if (ProcessRegistry * own = ProcessRegistry::existing()) {
        own->merge_own(topics, nodes, services, actions);
      }
      continue;
    }

    // Read through a handle that lets the owner rewrite the file underneath
    // this read; see `read_shared`.
    std::string content;
    if (!detail::read_shared(item.path(), content)) {
      continue;
    }
    std::vector<TopicEntry> file_topics;
    std::vector<NodeEntry> file_nodes;
    std::vector<ServiceEntry> file_services;
    std::vector<ActionEntry> file_actions;
    bool terminated = false;
    std::string line;
    std::istringstream in(content);
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line == "#end") {
        terminated = true;
        break;
      }
      if (line.empty() || line[0] == '#') {
        continue;
      }
      const std::vector<std::string> fields = detail::split_tab(line);
      if (fields.size() >= 2 && fields[0] == "node") {
        file_nodes.push_back(NodeEntry{fields[1], fields.size() > 2 ? fields[2] : "local"});
      } else if (fields.size() >= 5 && fields[0] == "topic") {
        TopicEntry entry;
        entry.name = fields[1];
        entry.type = fields[2];
        entry.publishers = std::atoi(fields[3].c_str());
        entry.subscribers = std::atoi(fields[4].c_str());
        entry.source = fields.size() > 5 ? fields[5] : "local";
        file_topics.push_back(entry);
      } else if (fields.size() >= 3 && fields[0] == "service") {
        ServiceEntry entry;
        entry.name = fields[1];
        entry.type = fields[2];
        entry.source = fields.size() > 3 ? fields[3] : "local";
        file_services.push_back(entry);
      } else if (fields.size() >= 3 && fields[0] == "action") {
        ActionEntry entry;
        entry.name = fields[1];
        entry.type = fields[2];
        entry.source = fields.size() > 3 ? fields[3] : "local";
        file_actions.push_back(entry);
      }
    }
    if (!terminated) {
      continue;  // the owner is mid-rewrite
    }
    for (const auto & entry : file_topics) {
      merge_topic(topics, entry);
    }
    nodes.insert(nodes.end(), file_nodes.begin(), file_nodes.end());
    if (services != nullptr) {
      for (const auto & entry : file_services) {
        merge_service(*services, entry);
      }
    }
    if (actions != nullptr) {
      for (const auto & entry : file_actions) {
        merge_action(*actions, entry);
      }
    }
  }

  // Sweeping a dead process' file above can leave the directory empty, and an
  // empty directory is the same answer the registry's destructor acts on:
  // nobody else is in this namespace, so the directory can go. Without this a
  // process that was *killed* rather than shut down leaves its file behind, the
  // sweep removes the file, and the empty directory then stays in `%TEMP%` for
  // good - that is where the 37 of them came from. A live sibling keeps the
  // directory (non-empty, `remove` says no), and one that is between
  // `create_directories` and its first write recreates it: the same retry that
  // already exists for a refused swap.
  if (remove_stale) {
    std::error_code remove_error;
    std::filesystem::remove(dir, remove_error);
  }
}

inline std::vector<TopicEntry> list_topics(bool remove_stale = true)
{
  std::vector<TopicEntry> topics;
  std::vector<NodeEntry> nodes;
  collect(topics, nodes, remove_stale);
  std::sort(
    topics.begin(), topics.end(),
    [](const TopicEntry & a, const TopicEntry & b) {return a.name < b.name;});
  return topics;
}

// The merged view the peer counts are answered from, refreshed at most this
// often. Everything here is a set of files that every process rewrites when its
// own graph changes, so re-reading them per call would put a directory scan in
// the middle of the controller's 10 Hz loop.
inline std::vector<TopicEntry> merged_topics_cached()
{
  constexpr auto period = std::chrono::milliseconds(250);
  static std::mutex mutex;
  static std::chrono::steady_clock::time_point refreshed{};
  static std::vector<TopicEntry> topics;

  std::lock_guard<std::mutex> lock(mutex);
  const auto now = std::chrono::steady_clock::now();
  if (refreshed.time_since_epoch().count() == 0 || now - refreshed >= period) {
    topics = list_topics();
    refreshed = now;
  }
  return topics;
}

// How many subscriptions exist for `topic` - in this process, in the other
// processes on this machine, and mirrored in from a peer by the bridge.
//
// Answering from this process alone (what the registry's own records cover)
// looks harmless until you notice what the callers do with it: nav2 gates
// publishing on this count. The planner only puts a plan on /plan when
// somebody is listening, the controller only writes /cmd_vel when it sees a
// subscriber, the costmap only publishes /costmap when RViz asked for it. On
// this port "somebody" is normally another process - a second shim binary, or
// the bridge's own reader - so a process-local count is a silent zero and the
// topic simply never appears, with no error to explain why.
//
// A ROS2 publisher knows these subscribers through DDS discovery; the shim's
// equivalent is the graph the bridge also writes into, which is why the merged
// view is the one that matches what callers mean.
inline std::size_t subscription_count(const std::string & topic)
{
  std::size_t count = 0;
  for (const auto & entry : merged_topics_cached()) {
    if (entry.name == topic) {
      count += static_cast<std::size_t>(entry.subscribers);
    }
  }
  return count;
}

// The same for `rclcpp::Node::count_publishers()`: `nav2_velocity_smoother`
// decides from it whether it is the one driving `/cmd_vel` or someone else is.
inline std::size_t publisher_count(const std::string & topic)
{
  std::size_t count = 0;
  for (const auto & entry : merged_topics_cached()) {
    if (entry.name == topic) {
      count += static_cast<std::size_t>(entry.publishers);
    }
  }
  return count;
}

inline std::vector<ServiceEntry> list_services(bool remove_stale = true)
{
  std::vector<TopicEntry> topics;
  std::vector<NodeEntry> nodes;
  std::vector<ServiceEntry> services;
  collect(topics, nodes, remove_stale, &services);
  std::sort(
    services.begin(), services.end(),
    [](const ServiceEntry & a, const ServiceEntry & b) {return a.name < b.name;});
  return services;
}

inline std::vector<ActionEntry> list_actions(bool remove_stale = true)
{
  std::vector<TopicEntry> topics;
  std::vector<NodeEntry> nodes;
  std::vector<ActionEntry> actions;
  collect(topics, nodes, remove_stale, nullptr, &actions);
  std::sort(
    actions.begin(), actions.end(),
    [](const ActionEntry & a, const ActionEntry & b) {return a.name < b.name;});
  return actions;
}

inline std::vector<NodeEntry> list_nodes(bool remove_stale = true)
{
  std::vector<TopicEntry> topics;
  std::vector<NodeEntry> nodes;
  collect(topics, nodes, remove_stale);
  std::sort(
    nodes.begin(), nodes.end(),
    [](const NodeEntry & a, const NodeEntry & b) {return a.name < b.name;});
  return nodes;
}

// Convenience wrappers returning just the names, the way `ros2 topic list`,
// `ros2 service list` and `ros2 node list` print them.
inline std::vector<std::string> topic_names(bool remove_stale = true)
{
  std::vector<std::string> names;
  for (const auto & entry : list_topics(remove_stale)) {
    names.push_back(entry.name);
  }
  return names;
}

inline std::vector<std::string> node_names(bool remove_stale = true)
{
  std::vector<std::string> names;
  for (const auto & entry : list_nodes(remove_stale)) {
    names.push_back(entry.name);
  }
  return names;
}

inline std::vector<std::string> service_names(bool remove_stale = true)
{
  std::vector<std::string> names;
  for (const auto & entry : list_services(remove_stale)) {
    names.push_back(entry.name);
  }
  return names;
}

inline std::vector<std::string> action_names(bool remove_stale = true)
{
  std::vector<std::string> names;
  for (const auto & entry : list_actions(remove_stale)) {
    names.push_back(entry.name);
  }
  return names;
}

// The question `rclcpp_action::Client` is really asking before it sends a goal:
// is anybody serving this action, on this machine or mirrored in from a peer?
//
// Deliberately not cached the way `merged_topics_cached()` is: this is asked
// once per client (and by a robot-facing caller that has nothing else to do),
// and a stale "no" is the failure that matters - it would make a goal that is
// one millisecond away from being servable look impossible.
inline bool action_exists(const std::string & name, bool remove_stale = true)
{
  const std::string wanted = qualified_action_name(name);
  for (const auto & entry : list_actions(remove_stale)) {
    if (entry.name == wanted) {
      return true;
    }
  }
  return false;
}

inline std::string action_type(const std::string & name, bool remove_stale = true)
{
  const std::string wanted = qualified_action_name(name);
  for (const auto & entry : list_actions(remove_stale)) {
    if (entry.name == wanted) {
      return entry.type;
    }
  }
  return std::string();
}

// Waits for an action server to be advertised, exactly the way
// `wait_for_service()` below waits for a service: immediately when it is
// already there, `false` once `timeout` has passed, and never before that for
// a negative timeout - the shim's convention for "wait forever".
template<class RepT, class PeriodT>
inline bool wait_for_action(
  const std::string & name, const std::chrono::duration<RepT, PeriodT> & timeout)
{
  const auto limit = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
  const auto deadline = std::chrono::steady_clock::now() + limit;
  for (;;) {
    if (action_exists(name)) {
      return true;
    }
    if (limit.count() >= 0 && std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

// The question `rclcpp::Client` is really asking: is anybody serving this name,
// on this machine or mirrored in from a peer?
inline bool service_exists(const std::string & name, bool remove_stale = true)
{
  for (const auto & entry : list_services(remove_stale)) {
    if (entry.name == name) {
      return true;
    }
  }
  return false;
}

// Waits for a service to be advertised, the way upstream's
// `rclcpp::Client::wait_for_service()` does, and returns whether it is there:
// immediately when it already is, `false` once `timeout` has passed, and never
// before if `timeout` is negative - the shim's convention for "no timeout",
// which callers that poll in their own loop (nav2's `ServiceClient::invoke()`
// does, once a second) rely on.
template<class RepT, class PeriodT>
inline bool wait_for_service(
  const std::string & name, const std::chrono::duration<RepT, PeriodT> & timeout)
{
  const auto limit = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
  const auto deadline = std::chrono::steady_clock::now() + limit;
  for (;;) {
    if (service_exists(name)) {
      return true;
    }
    if (limit.count() >= 0 && std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

}  // namespace graph

// Maps a generated message struct (see tools/generate_messages.py) onto its
// ROS2 type name, e.g. sensor_msgs/msg/LaserScan. Types that did not come from
// the generator fall back to an empty name.
template<class MessageT, class = void>
struct MessageTypeName
{
  static const char * get() {return "";}
};

template<class MessageT>
struct MessageTypeName<MessageT, std::void_t<decltype(MessageT::ros2_message_type)>>
{
  static const char * get() {return MessageT::ros2_message_type;}
};

template<class MessageT>
inline const char * message_type_name()
{
  return MessageTypeName<MessageT>::get();
}

// The same for the generated service structs, which carry the name
// `lifecycle_msgs/srv/ChangeState`. Only used to label the graph record.
template<class ServiceT, class = void>
struct ServiceTypeName
{
  static const char * get() {return "";}
};

template<class ServiceT>
struct ServiceTypeName<ServiceT, std::void_t<decltype(ServiceT::ros2_service_type)>>
{
  static const char * get() {return ServiceT::ros2_service_type;}
};

template<class ServiceT>
inline const char * service_type_name()
{
  return ServiceTypeName<ServiceT>::get();
}

}  // namespace ros2_shim

#endif  // ROS2_SHIM_GRAPH_HPP_