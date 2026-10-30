// Live probe: dump an OccupancyGrid from the shared-memory graph to a PGM.
//
// Like tf_probe this is not a ctest - it needs the bridge daemon (and the
// Ubuntu peer) to be up, so it is run by hand:
//
//     call bridge\windows\bridge_env.bat
//     ros2_shim\build\map_dump.exe --topic /slam_map --settle 5 --out slam_map.pgm
//
// `ros2 topic echo` truncates an OccupancyGrid to "[N cells, M occupied]",
// which is enough to see that *something* arrived but not whether the map is
// any good. This tool keeps the grid and renders it, and it also prints a
// coarse ASCII preview so the result can be checked over a terminal.
//
// The PGM it writes follows the ROS map-file convention - first row of the
// file is the highest y - so it is the same picture as map_saver's, and
// map_server reads it back as the grid it came from.
#include "rclcpp/rclcpp.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace
{

const char * kUsage =
  "usage: map_dump [options]\n"
  "  --topic NAME     topic to read            (default /slam_map)\n"
  "  --out PATH       PGM to write             (default map_dump.pgm)\n"
  "  --duration S     give up after S seconds  (default 60)\n"
  "  --settle S       stop S seconds after the first grid arrives (default 0)\n"
  "  --ascii N        preview width in characters, 0 disables (default 72)\n";

// nav2/map_server colour convention: free 254, occupied 0, unknown 205.
unsigned char shade(int8_t value)
{
  if (value < 0) {
    return 205;
  }
  if (value == 0) {
    return 254;
  }
  return static_cast<unsigned char>(100 - value) * 254 / 100;
}

bool writePgm(
  const std::string & path, unsigned width, unsigned height, const std::vector<int8_t> & data)
{
  std::FILE * file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    std::fprintf(stderr, "[map_dump] cannot open %s for writing\n", path.c_str());
    return false;
  }
  std::fprintf(file, "P5\n%u %u\n255\n", width, height);
  // Row 0 of an OccupancyGrid is the *lowest* y, but the first row of a ROS
  // map file is the highest one - map_server flips on the way in and
  // map_saver on the way out. Emitting the grid in its own order, which this
  // tool used to do, left a PGM that came back from map_server mirrored about
  // the x axis; it only looked right because printAscii below already walked
  // the rows in reverse.
  for (std::size_t row = 0; row < height; ++row) {
    const std::size_t y = height - 1 - row;
    for (std::size_t x = 0; x < width; ++x) {
      std::fputc(shade(data[y * width + x]), file);
    }
  }
  std::fclose(file);
  return true;
}

// Rows come out of the grid bottom-up, so walk them in reverse to print the
// picture the way it is normally drawn.
void printAscii(const nav_msgs::msg::OccupancyGrid & grid, int columns, bool flip)
{
  if (columns <= 0 || grid.info.width == 0 || grid.info.height == 0) {
    return;
  }
  const int rows = static_cast<int>(
    static_cast<double>(columns) * grid.info.height / grid.info.width * 0.5);
  for (int row = 0; row < rows; ++row) {
    std::string line;
    for (int column = 0; column < columns; ++column) {
      const unsigned x = static_cast<unsigned>(column * grid.info.width / columns);
      unsigned y = static_cast<unsigned>(row * grid.info.height / rows);
      if (!flip) {
        y = grid.info.height - 1 - y;
      }
      const int8_t value = grid.data[y * grid.info.width + x];
      line.push_back(value < 0 ? ' ' : (value == 0 ? '.' : (value > 65 ? '#' : '+')));
    }
    std::printf("MAPDUMP |%s|\n", line.c_str());
    std::fflush(stdout);
  }
}

}  // namespace

int main(int argc, char ** argv)
{
  std::string topic = "/slam_map";
  std::string out = "map_dump.pgm";
  double duration = 60.0;
  double settle = 0.0;
  int ascii_columns = 72;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto next = [&](const char * option) -> const char * {
        if (i + 1 >= argc) {
          std::fprintf(stderr, "[map_dump] missing value for %s\n", option);
          std::exit(2);
        }
        return argv[++i];
      };
    if (arg == "--help" || arg == "-h") {
      std::fputs(kUsage, stdout);
      return 0;
    } else if (arg == "--topic") {
      topic = next("--topic");
    } else if (arg == "--out") {
      out = next("--out");
    } else if (arg == "--duration") {
      duration = std::strtod(next("--duration"), nullptr);
    } else if (arg == "--settle") {
      settle = std::strtod(next("--settle"), nullptr);
    } else if (arg == "--ascii") {
      ascii_columns = std::atoi(next("--ascii"));
    } else {
      std::fprintf(stderr, "[map_dump] unknown option '%s'\n%s", arg.c_str(), kUsage);
      return 2;
    }
  }

  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("map_dump");

  nav_msgs::msg::OccupancyGrid latest;
  int frames = 0;
  auto subscription = node->create_subscription<nav_msgs::msg::OccupancyGrid>(
    topic, rclcpp::QoS(rclcpp::KeepLast(1)).transient_local(),
    [&](nav_msgs::msg::OccupancyGrid::ConstSharedPtr message) {
      latest = *message;
      ++frames;
    });

  const auto start = std::chrono::steady_clock::now();
  const auto deadline = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
    std::chrono::duration<double>(duration));
  auto settle_deadline = start;
  while (rclcpp::ok() && std::chrono::steady_clock::now() < deadline) {
    node->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto now = std::chrono::steady_clock::now();
    if (frames > 0 && settle > 0.0) {
      if (settle_deadline == start) {
        settle_deadline = now +
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(settle));
      } else if (now >= settle_deadline) {
        break;
      }
    }
  }

  if (frames == 0) {
    std::printf("MAPDUMP none on %s after %.1fs\n", topic.c_str(), duration);
    std::fflush(stdout);
    rclcpp::shutdown();
    return 1;
  }

  int occupied = 0;
  int free_cells = 0;
  int unknown = 0;
  for (const int8_t value : latest.data) {
    if (value < 0) {
      ++unknown;
    } else if (value == 0) {
      ++free_cells;
    } else {
      ++occupied;
    }
  }

  std::printf(
    "MAPDUMP frames=%d frame=%s size=%ux%u res=%.4f origin=(%.3f, %.3f) "
    "occupied=%d free=%d unknown=%d\n",
    frames, latest.header.frame_id.c_str(), latest.info.width, latest.info.height,
    latest.info.resolution, latest.info.origin.position.x, latest.info.origin.position.y,
    occupied, free_cells, unknown);
  std::fflush(stdout);

  if (!writePgm(out, latest.info.width, latest.info.height, latest.data)) {
    rclcpp::shutdown();
    return 1;
  }
  std::printf("MAPDUMP wrote %s\n", out.c_str());
  std::fflush(stdout);

  printAscii(latest, ascii_columns, false);
  rclcpp::shutdown();
  return 0;
}
