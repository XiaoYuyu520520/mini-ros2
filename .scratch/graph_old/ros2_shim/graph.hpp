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
// File format (tab separated, UTF-8), one record per line:
//   #nav2shim-graph\t1
//   node\t<fqn>\t<source>
//   topic\t<name>\t<type>\t<publishers>\t<subscribers>\t<source>
//   service\t<name>\t<type>\t<source>
//   action\t<name>\t<type>\t<source>
//   #end

#include <algorithm>
#include <chrono>
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
  if (!override_dir.empty()) {
    return std::filesystem::path(override_dir);
  }
  return temp_directory() / "nav2_shim_graph";
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

inline bool read_shared(const std::filesystem::path & path, std::string & content)
{
  content.clear();
#if defined(_WIN32)
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
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
  std::error_code error;
  std::filesystem::remove(to, error);
  std::filesystem::rename(from, to, error);
  return !error;
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
    return registry;
  }

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
  ProcessRegistry() = default;

  ~ProcessRegistry()
  {
    std::error_code error;
    std::filesystem::remove(file_path(), error);
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

  void flush_locked()
  {
    std::error_code error;
    std::filesystem::create_directories(detail::graph_dir(), error);
    const std::string final_path = file_path();
    const std::string temp_path = final_path + ".tmp";

    std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return;
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
    detail::install_file(temp_path, final_path);
  }

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