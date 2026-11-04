// The graph's file cache must not serve a stale answer.
//
// The cache exists because reading the graph means opening and parsing every
// `<pid>.graph` file, and a graph rarely changes between two refreshes. The
// risk it brings is obvious and worth pinning down: a file that changed must
// not be answered from the cache, and a file that is gone must not be served.
//
// The reads here pass `remove_stale = false`, because the file carries a PID
// that does not exist and the sweeper would delete it before the cache could
// be exercised. The last check then reads with the sweeper on, which is the
// path a real dead process takes: its file is removed and its topic is gone
// from the very next listing.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "ros2_shim/graph.hpp"

namespace
{

int failures = 0;

void check(bool ok, const char * what)
{
  std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++failures;
  }
}

// A graph file as the registry writes it: a header, one topic, and the
// terminator that says "this file is complete".
void write_graph(const std::filesystem::path & path, const std::string & topic)
{
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << "#nav2shim-graph\t1\n";
  out << "topic\t" << topic << "\tstd_msgs/msg/String\t1\t0\tlocal\n";
  out << "#end\n";
  out.close();
}

bool listing_has(const std::vector<ros2_shim::graph::TopicEntry> & topics,
                 const std::string & name)
{
  for (const auto & entry : topics) {
    if (entry.name == name) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main()
{
  const std::filesystem::path dir = ros2_shim::graph::detail::graph_dir();
  std::error_code error;
  std::filesystem::create_directories(dir, error);

  // A file that looks like a graph file of another process. The PID is not
  // alive, so every read below uses `remove_stale = false` to keep the file
  // in place while the cache is exercised.
  const std::filesystem::path file = dir / "999999.graph";
  write_graph(file, "/cached_topic");

  ros2_shim::graph::graph_file_cache().clear();

  // 1. The first read parses the file.
  const auto first = ros2_shim::graph::list_topics(false);
  check(listing_has(first, "/cached_topic"), "the first read sees the topic");
  check(
    ros2_shim::graph::graph_file_cache().size() == 1,
    "the parsed file is in the cache");

  // 2. The second read is answered from the cache - the file did not change.
  const auto second = ros2_shim::graph::list_topics(false);
  check(listing_has(second, "/cached_topic"), "the cached read sees the same topic");
  check(
    ros2_shim::graph::graph_file_cache().size() == 1,
    "the cached read did not grow the cache");

  // 3. A file that changed is re-read. The rewrite changes its modification
  //    time, which is the identity the cache is keyed on; the sleep makes
  //    sure the timestamp really differs.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  write_graph(file, "/changed_topic");

  const auto third = ros2_shim::graph::list_topics(false);
  check(listing_has(third, "/changed_topic"), "a changed file is re-read");
  check(!listing_has(third, "/cached_topic"), "the old contents are not served");

  // 4. A file that is gone is not served. With the sweeper on, a dead
  //    process' file is removed and its topic disappears from the listing -
  //    what a real dead process looks like from the outside.
  std::filesystem::remove(file, error);
  const auto fourth = ros2_shim::graph::list_topics(true);
  check(!listing_has(fourth, "/changed_topic"), "a file that is gone is not served");

  std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}
