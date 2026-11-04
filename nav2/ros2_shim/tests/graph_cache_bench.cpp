// How much the graph's file cache saves.
//
// The cache exists because a refresh re-reads and re-parses every graph file
// of every process on the machine, and a graph rarely changes between two
// refreshes. This measures the two paths: the first read (parse) and the
// reads after it (cached).
//
// Run: nav2/ros2_shim/build/graph_cache_bench

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ros2_shim/graph.hpp"

namespace
{

void write_graph(const std::filesystem::path & path, int index)
{
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << "#nav2shim-graph\t1\n";
  for (int i = 0; i < 20; ++i) {
    out << "topic\t/topic_" << index << "_" << i << "\tstd_msgs/msg/String\t1\t0\tlocal\n";
  }
  out << "#end\n";
  out.close();
}

}  // namespace

int main()
{
  const std::filesystem::path dir = ros2_shim::graph::detail::graph_dir();
  std::error_code error;
  std::filesystem::create_directories(dir, error);

  // Ten files, the size of a nav2 stack's worth of processes.
  const int files = 10;
  for (int i = 0; i < files; ++i) {
    write_graph(dir / (std::to_string(900000 + i) + ".graph"), i);
  }
  ros2_shim::graph::graph_file_cache().clear();

  const auto start = std::chrono::steady_clock::now();
  const auto first = ros2_shim::graph::list_topics(false);
  const auto first_ms = std::chrono::duration<double, std::milli>(
    std::chrono::steady_clock::now() - start).count();

  const int rounds = 100;
  const auto cached_start = std::chrono::steady_clock::now();
  std::size_t topics = 0;
  for (int i = 0; i < rounds; ++i) {
    topics = ros2_shim::graph::list_topics(false).size();
  }
  const auto cached_ms = std::chrono::duration<double, std::milli>(
    std::chrono::steady_clock::now() - cached_start).count();

  std::printf("graph cache: %d files, %zu topics\n", files, topics);
  std::printf("  first read (parse)      %8.3f ms\n", first_ms);
  std::printf("  cached read             %8.3f ms  (%zu reads)\n",
              cached_ms / rounds, rounds);
  std::printf("  speed-up                %8.1fx\n", first_ms / (cached_ms / rounds));
  return 0;
}
