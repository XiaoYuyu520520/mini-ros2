// bridge_cdr_check - byte exact verification of the CDR <-> shim transcoder.
//
// Every fixture under bridge/tests/fixtures was produced by two independent
// implementations (see bridge/tests/generate_fixtures.py):
//
//   <name>.cdr   ROS2 on-the-wire CDR from the `rosbags` type store
//   <name>.shim  the shared-memory layout parsed out of the shim's own
//                generated headers
//
// The test asserts, for every fixture:
//
//   1. cdr_to_shim(cdr)               == shim     (decoder + shim layout)
//   2. shim_to_cdr(shim)              == cdr      (encoder + CDR alignment)
//   3. shim_to_cdr(cdr_to_shim(cdr))  == cdr      (full round trip)
//
// so a wrong alignment rule anywhere in the transcoder shows up as a byte
// mismatch instead of silently corrupting nav2's sensor data.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "ros2_bridge/type_registry.hpp"

namespace
{

int g_failures = 0;
int g_checks = 0;

std::string fixtureDir()
{
  const char * from_env = std::getenv("ROS2_BRIDGE_FIXTURES");
  if (from_env != nullptr && *from_env != '\0') {
    return from_env;
  }
#ifdef ROS2_BRIDGE_FIXTURE_DIR
  return ROS2_BRIDGE_FIXTURE_DIR;
#else
  return "fixtures";
#endif
}

std::vector<std::string> split(const std::string & text, char separator)
{
  std::vector<std::string> parts;
  std::string current;
  for (char c : text) {
    if (c == separator) {
      parts.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  parts.push_back(current);
  return parts;
}

std::string trim(const std::string & text)
{
  std::size_t begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  std::size_t end = text.find_last_not_of(" \t\r\n");
  return text.substr(begin, end - begin + 1);
}

bool readFile(const std::string & path, ros2_bridge::Bytes & out)
{
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    std::fprintf(stderr, "cannot open %s\n", path.c_str());
    return false;
  }
  out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  return true;
}

// First byte where the two buffers differ, or -1 when they are identical.
long firstDifference(const ros2_bridge::Bytes & a, const ros2_bridge::Bytes & b)
{
  const std::size_t limit = a.size() < b.size() ? a.size() : b.size();
  for (std::size_t i = 0; i < limit; ++i) {
    if (a[i] != b[i]) {
      return static_cast<long>(i);
    }
  }
  return a.size() == b.size() ? -1 : static_cast<long>(limit);
}

void dumpAround(const char * label, const ros2_bridge::Bytes & data, long offset)
{
  const long begin = offset > 8 ? offset - 8 : 0;
  const long end = offset + 8 < static_cast<long>(data.size())
    ? offset + 8 : static_cast<long>(data.size());
  std::fprintf(stderr, "    %s [%ld..%ld):", label, begin, end);
  for (long i = begin; i < end; ++i) {
    std::fprintf(stderr, " %02x", data[static_cast<std::size_t>(i)]);
  }
  std::fprintf(stderr, "\n");
}

void expectEqual(
  const std::string & what, const ros2_bridge::Bytes & actual,
  const ros2_bridge::Bytes & expected)
{
  ++g_checks;
  const long offset = firstDifference(actual, expected);
  if (offset < 0) {
    return;
  }
  ++g_failures;
  std::fprintf(
    stderr, "  FAIL %s: sizes actual=%zu expected=%zu, first difference at byte %ld\n",
    what.c_str(), actual.size(), expected.size(), offset);
  dumpAround("actual  ", actual, offset);
  dumpAround("expected", expected, offset);
}

}  // namespace

int main(int argc, char ** argv)
{
  // `--describe` also prints what `ros2 topic echo` would show for every
  // fixture: the byte comparisons prove the transcoder, this proves the printer
  // that reads it, which is what a person debugging a topic actually sees.
  bool show_descriptions = false;
  std::string dir;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--describe") == 0) {
      show_descriptions = true;
    } else if (dir.empty()) {
      dir = argv[i];
    }
  }
  if (dir.empty()) {
    dir = fixtureDir();
  }
  if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
    dir.push_back('/');
  }

  std::ifstream manifest(dir + "manifest.tsv");
  if (!manifest) {
    std::fprintf(stderr, "cannot open %smanifest.tsv\n", dir.c_str());
    return 2;
  }

  int fixtures = 0;
  std::string line;
  while (std::getline(manifest, line)) {
    const std::string entry = trim(line);
    if (entry.empty() || entry[0] == '#') {
      continue;
    }
    const std::vector<std::string> parts = split(entry, '\t');
    if (parts.size() < 2) {
      std::fprintf(stderr, "malformed manifest line: %s\n", entry.c_str());
      return 2;
    }
    const std::string name = parts[0];
    const std::string type = parts[1];
    ++fixtures;

    if (!ros2_bridge::is_supported(type)) {
      std::fprintf(stderr, "  FAIL %s: type '%s' is not in the bridge registry\n",
        name.c_str(), type.c_str());
      ++g_failures;
      continue;
    }

    ros2_bridge::Bytes cdr;
    ros2_bridge::Bytes reference_shim;
    if (!readFile(dir + name + ".cdr", cdr) ||
      !readFile(dir + name + ".shim", reference_shim))
    {
      ++g_failures;
      continue;
    }

    ros2_bridge::Bytes shim;
    if (!ros2_bridge::cdr_to_shim(type, cdr, shim)) {
      std::fprintf(stderr, "  FAIL %s: cdr_to_shim rejected the payload\n", name.c_str());
      ++g_failures;
      continue;
    }

    ros2_bridge::Bytes cdr_back;
    if (!ros2_bridge::shim_to_cdr(type, reference_shim, cdr_back)) {
      std::fprintf(stderr, "  FAIL %s: shim_to_cdr rejected the payload\n", name.c_str());
      ++g_failures;
      continue;
    }

    ros2_bridge::Bytes round_trip;
    if (!ros2_bridge::shim_to_cdr(type, shim, round_trip)) {
      std::fprintf(stderr, "  FAIL %s: round trip encoder rejected the payload\n", name.c_str());
      ++g_failures;
      continue;
    }

    const int before = g_failures;
    expectEqual(name + " cdr_to_shim", shim, reference_shim);
    expectEqual(name + " shim_to_cdr", cdr_back, cdr);
    expectEqual(name + " round_trip", round_trip, cdr);

    const std::string description = ros2_bridge::describe_shim(type, shim);
    // An empty description is a failure only when there was something to
    // describe: `std_msgs/msg/Empty` has no fields, and the CLI prints just its
    // `---` separator for it.
    if (description.empty() && !shim.empty()) {
      std::fprintf(stderr, "  FAIL %s: describe_shim produced nothing\n", name.c_str());
      ++g_failures;
    }
    // `describe` falls back to this string when no printer matches the type,
    // which is non-empty and would otherwise let a type sit in the registry
    // with `ros2 topic echo` showing nothing useful.
    if (description.find("(no pretty printer registered") != std::string::npos) {
      std::fprintf(stderr, "  FAIL %s: no pretty printer for %s\n",
        name.c_str(), type.c_str());
      ++g_failures;
    }

    if (g_failures == before) {
      std::printf(
        "  ok   %-32s %-45s cdr=%zu shim=%zu\n", name.c_str(), type.c_str(),
        cdr.size(), shim.size());
      if (show_descriptions) {
        std::printf("       %s\n", description.c_str());
      }
    }
  }

  std::printf(
    "\n%zu fixtures, %d byte comparisons, %d failure(s)\n",
    static_cast<std::size_t>(fixtures), g_checks, g_failures);
  if (g_failures != 0) {
    return 1;
  }
  std::printf("CDR <-> shim transcoding is byte exact in both directions\n");
  return 0;
}