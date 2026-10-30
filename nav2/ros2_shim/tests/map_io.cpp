// The ported `nav2_map_server` map I/O, pinned down against its own writer.
//
// The port replaces Magick++ with its own stand-in (include/Magick++.h), so
// the thing worth testing is not "does the code compile" but "does a map
// survive the trip through a file that another tool could also read":
//
//   1. a trinary map saved and loaded again comes back cell for cell,
//   2. the file really is a P5 PGM with the rows in image order (the ROS map's
//      origin is bottom-left, the image's is top-left, and one of the two has
//      to be flipped exactly once),
//   3. the YAML sidecar says what nav2's own map_saver says,
//   4. P2 (ascii) and 15-bit P5 files - what a map from another tool can look
//      like - are read with the same gray levels,
//   5. the failure paths keep their meaning: a missing file is not the same
//      error as a corrupt one, a PNG that stops in the middle of its own
//      chunks is refused rather than read as a PGM, and a format this build
//      has no coder for falls back to the one it does have,
//   6. and PNG round trips: a trinary map goes out and comes back as 8-bit
//      grayscale, and a scale-mode map keeps its alpha channel - which is
//      the only reason map_saver defaults to png in that mode.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "yaml-cpp/yaml.h"

#include "nav2_map_server/map_io.hpp"
#include "nav2_map_server/map_mode.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "rclcpp/rclcpp.hpp"

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

void checkEqual(size_t actual, size_t expected, const std::string & what)
{
  char text[256];
  std::snprintf(text, sizeof(text), "%s (got %zu, expected %zu)", what.c_str(), actual, expected);
  check(actual == expected, text);
}

std::string readTextFile(const std::string & path)
{
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::vector<unsigned char> readBytes(const std::string & path)
{
  std::ifstream stream(path, std::ios::binary);
  return std::vector<unsigned char>(
    std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void writeBytes(const std::string & path, const std::vector<unsigned char> & bytes)
{
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char *>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
}

void writeText(const std::string & path, const std::string & text)
{
  std::ofstream stream(path, std::ios::binary);
  stream << text;
}

nav_msgs::msg::OccupancyGrid makeGrid(
  size_t width, size_t height, double resolution, const std::vector<int8_t> & data)
{
  nav_msgs::msg::OccupancyGrid grid;
  // nav2::validateMsg() refuses a map with no frame_id, and a real one is in
  // `map` anyway.
  grid.header.frame_id = "map";
  grid.info.width = static_cast<uint32_t>(width);
  grid.info.height = static_cast<uint32_t>(height);
  grid.info.resolution = resolution;
  grid.info.origin.position.x = 1.5;
  grid.info.origin.position.y = -2.25;
  grid.info.origin.orientation.w = 1.0;
  grid.data.assign(data.begin(), data.end());
  return grid;
}

bool sameData(const nav_msgs::msg::OccupancyGrid & grid, const std::vector<int8_t> & expected)
{
  return grid.data.size() == expected.size() &&
         std::equal(grid.data.begin(), grid.data.end(), expected.begin());
}

std::string describeData(const nav_msgs::msg::OccupancyGrid & grid)
{
  std::string text;
  for (size_t i = 0; i < grid.data.size(); ++i) {
    text += std::to_string(static_cast<int>(grid.data[i]));
    text += (i + 1 == grid.data.size()) ? "" : ",";
  }
  return text;
}

// A YAML sidecar pointing at an image next to it, the way a hand-written map
// looks - and the only way to exercise the P2 / 15-bit readers, because the
// saver only ever writes P5 at 255.
std::string writeSidecar(
  const std::string & dir, const std::string & name, const std::string & image,
  const std::string & mode = "trinary")
{
  writeText(
    dir + "/" + name + ".yaml",
    "image: " + image + "\n"
    "mode: " + mode + "\n"
    "resolution: 0.050\n"
    "origin: [0.000, 0.000, 0.000]\n"
    "negate: 0\n"
    "occupied_thresh: 0.65\n"
    "free_thresh: 0.196\n");
  return dir + "/" + name + ".yaml";
}

}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  const std::string dir = "map_io_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);

  // ---- 1. a trinary map goes out and comes back ---------------------------
  //
  // Three rows that are each a single value, so a row that lands in the wrong
  // place cannot hide: occupied at the bottom of the map, unknown in the
  // middle, free at the top.
  const std::vector<int8_t> trinary{
    100, 100, 100, 100,
    -1, -1, -1, -1,
    0, 0, 0, 0};
  const auto grid = makeGrid(4, 3, 0.05, trinary);

  nav2_map_server::SaveParameters save;
  save.map_file_name = dir + "/roundtrip";
  save.image_format = "pgm";
  save.mode = nav2_map_server::MapMode::Trinary;
  check(nav2_map_server::saveMapToFile(grid, save), "saveMapToFile() writes a trinary map");

  // ---- 2. the file is a P5 PGM, rows in image order ------------------------
  const auto bytes = readBytes(dir + "/roundtrip.pgm");
  const std::string header = "P5\n4 3\n255\n";
  check(
    bytes.size() == header.size() + 12 && std::equal(
      header.begin(), header.end(), bytes.begin()),
    "the file is P5, 4x3, maxval 255");
  if (bytes.size() == header.size() + 12) {
    // map row 2 (free, 0) -> image row 0 -> 254; map row 0 (occupied) -> image
    // row 2 -> 0; unknown -> 205. That is nav2's trinary palette exactly.
    const std::vector<unsigned char> expected{
      254, 254, 254, 254,
      205, 205, 205, 205,
      0, 0, 0, 0};
    const std::vector<unsigned char> pixels(bytes.begin() + header.size(), bytes.end());
    check(pixels == expected, "the rows are flipped once, into image order");
  }

  // ---- 3. the sidecar is the one nav2 writes ------------------------------
  const std::string sidecar_text = readTextFile(dir + "/roundtrip.yaml");
  check(!sidecar_text.empty(), "the YAML sidecar was written");
  if (!sidecar_text.empty()) {
  const YAML::Node sidecar = YAML::Load(sidecar_text);
  check(sidecar["image"].as<std::string>() == "roundtrip.pgm", "the sidecar names the image");
  check(sidecar["mode"].as<std::string>() == "trinary", "the sidecar keeps the mode");
  check(sidecar["resolution"].as<std::string>() == "0.050", "the sidecar keeps the resolution");
  check(sidecar["negate"].as<int>() == 0, "the sidecar says negate: 0");
  // Trinary mode writes the thresholds that match its palette, not the ones it
  // was called with - that is what makes 205 mean "unknown" on the way back.
  check(sidecar["occupied_thresh"].as<std::string>() == "0.65", "the trinary occupied threshold");
  check(sidecar["free_thresh"].as<std::string>() == "0.196", "the trinary free threshold");
  checkEqual(sidecar["origin"].size(), 3, "the sidecar origin has three elements");
  }

  // ---- 4. and it loads back cell for cell --------------------------------
  nav_msgs::msg::OccupancyGrid loaded;
  check(
    nav2_map_server::loadMapFromYaml(dir + "/roundtrip.yaml", loaded) ==
    nav2_map_server::LOAD_MAP_SUCCESS,
    "loadMapFromYaml() reads the pair back");
  check(sameData(loaded, trinary), "the trinary grid round trips: " + describeData(loaded));
  checkEqual(loaded.info.width, 4, "the width survives");
  checkEqual(loaded.info.height, 3, "the height survives");
  check(loaded.info.resolution > 0.0499 && loaded.info.resolution < 0.0501, "the resolution survives");
  check(
    loaded.info.origin.position.x > 1.499 && loaded.info.origin.position.x < 1.501 &&
    loaded.info.origin.position.y < -2.249,
    "the origin survives");

  // ---- 5. raw mode is lossless for the values a map can hold -------------
  const std::vector<int8_t> raw{0, 1, 50, 99, 100, -1};
  const auto raw_grid = makeGrid(3, 2, 0.1, raw);
  nav2_map_server::SaveParameters raw_save;
  raw_save.map_file_name = dir + "/raw";
  raw_save.image_format = "pgm";
  raw_save.mode = nav2_map_server::MapMode::Raw;
  check(nav2_map_server::saveMapToFile(raw_grid, raw_save), "saveMapToFile() writes a raw map");
  nav_msgs::msg::OccupancyGrid raw_loaded;
  check(
    nav2_map_server::loadMapFromYaml(dir + "/raw.yaml", raw_loaded) ==
    nav2_map_server::LOAD_MAP_SUCCESS,
    "loadMapFromYaml() reads a raw map back");
  check(sameData(raw_loaded, raw), "the raw grid round trips: " + describeData(raw_loaded));

  // ---- 6. files this build did not write ---------------------------------
  // P2, ascii, maxval 15: the gray levels are relative to maxval, so 0 is not
  // "black" unless maxval says so.
  writeText(dir + "/ascii.pgm", "P2\n# a comment\n2 2\n15\n0 5\n10 15\n");
  nav_msgs::msg::OccupancyGrid ascii;
  check(
    nav2_map_server::loadMapFromYaml(writeSidecar(dir, "ascii", "ascii.pgm"), ascii) ==
    nav2_map_server::LOAD_MAP_SUCCESS,
    "loadMapFromYaml() reads a P2 file");
  // 0 is the occupied end and maxval the free end, so a file whose maxval is not
  // 255 still lands on nav2's palette: 0 and 5 (of 15) are occupied, 10 is in
  // between, 15 is free - and the flip puts the image's first row last.
  check(
    sameData(ascii, {-1, 0, 100, 100}),
    "P2 samples are scaled by maxval: " + describeData(ascii));

  // P5 at 15 bits per pixel, most significant byte first.
  const std::vector<unsigned char> deep{
    'P', '5', '\n', '2', ' ', '2', '\n', '6', '5', '5', '3', '5', '\n',
    0, 0, 0x55, 0x55, 0xAA, 0xAA, 0xFF, 0xFF};
  writeBytes(dir + "/deep.pgm", deep);
  nav_msgs::msg::OccupancyGrid deep_grid;
  check(
    nav2_map_server::loadMapFromYaml(writeSidecar(dir, "deep", "deep.pgm"), deep_grid) ==
    nav2_map_server::LOAD_MAP_SUCCESS,
    "loadMapFromYaml() reads a 16-bit P5 file");
  check(
    sameData(deep_grid, {-1, 0, 100, 100}),
    "16-bit samples land on the same palette as 8-bit ones: " + describeData(deep_grid));

  // ---- 7. the failure paths ----------------------------------------------
  nav_msgs::msg::OccupancyGrid unused;
  check(
    nav2_map_server::loadMapFromYaml("", unused) == nav2_map_server::MAP_DOES_NOT_EXIST,
    "an empty YAML name is MAP_DOES_NOT_EXIST");
  check(
    nav2_map_server::loadMapFromYaml(dir + "/absent.yaml", unused) ==
    nav2_map_server::INVALID_MAP_METADATA,
    "a YAML that is not there is INVALID_MAP_METADATA");

  // A file that carries the PNG signature and nothing else. The reader has to
  // name the chunk it stopped in, not fall back to reading it as a PGM.
  const std::vector<unsigned char> png_signature{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  writeBytes(dir + "/fake.png", png_signature);
  check(
    nav2_map_server::loadMapFromYaml(writeSidecar(dir, "pngmap", "fake.png"), unused) ==
    nav2_map_server::INVALID_MAP_DATA,
    "a PNG that stops inside its first chunk is INVALID_MAP_DATA");

  // bmp is a format this build really has no coder for. `checkSaveParameters()`
  // catches that and falls back to png - the same fallback upstream's own build
  // takes - so the save succeeds into the file the fallback names.
  nav2_map_server::SaveParameters bmp_save;
  bmp_save.map_file_name = dir + "/no_coder";
  bmp_save.image_format = "bmp";
  bmp_save.mode = nav2_map_server::MapMode::Trinary;
  check(
    nav2_map_server::saveMapToFile(grid, bmp_save),
    "a format with no coder falls back to one this build has");
  check(
    !std::filesystem::exists(dir + "/no_coder.bmp") &&
    std::filesystem::exists(dir + "/no_coder.png"),
    "and the map lands in no_coder.png, with no .bmp left behind");

  // ---- 8. PNG ------------------------------------------------------------
  // The trinary grid again, through the coder map_saver reaches for when the
  // mode asks for transparency - and the only coder here that can carry it.
  nav2_map_server::SaveParameters png_save;
  png_save.map_file_name = dir + "/trinary_png";
  png_save.image_format = "png";
  png_save.mode = nav2_map_server::MapMode::Trinary;
  check(nav2_map_server::saveMapToFile(grid, png_save), "a trinary map saves as PNG");

  const std::vector<unsigned char> png_bytes = readBytes(dir + "/trinary_png.png");
  check(
    png_bytes.size() > 26 && std::equal(
      png_bytes.begin(), png_bytes.begin() + 8, png_signature.begin()),
    "the file really is a PNG (signature first)");
  // IHDR's data starts at 16 - width, height, then bit depth at 24 and colour
  // type at 25. A trinary map has no transparency to keep, so what comes out is
  // plain 8-bit grayscale rather than an alpha channel of 255s.
  check(
    png_bytes.size() > 26 && png_bytes[24] == 8 && png_bytes[25] == 0,
    "and it is 8-bit grayscale (IHDR bit depth 8, colour type 0)");

  nav_msgs::msg::OccupancyGrid png_loaded;
  check(
    nav2_map_server::loadMapFromYaml(dir + "/trinary_png.yaml", png_loaded) ==
    nav2_map_server::LOAD_MAP_SUCCESS,
    "loadMapFromYaml() reads the PNG pair back");
  check(
    sameData(png_loaded, trinary),
    "the trinary grid round trips through PNG: " + describeData(png_loaded));

  // Scale mode is the mode that writes transparency, so it is the one that
  // proves the alpha channel survives. Only the two ends of the scale and
  // "unknown" are used: a value between the thresholds is a number no coder
  // promises to give back exactly, and this test is about the channel.
  const std::vector<int8_t> scaled{100, 0, -1, -1};
  const auto scaled_grid = makeGrid(2, 2, 0.05, scaled);
  nav2_map_server::SaveParameters scale_save;
  scale_save.map_file_name = dir + "/scale_png";
  scale_save.image_format = "png";
  scale_save.mode = nav2_map_server::MapMode::Scale;
  check(nav2_map_server::saveMapToFile(scaled_grid, scale_save), "a scale-mode map saves as PNG");

  const std::vector<unsigned char> scale_bytes = readBytes(dir + "/scale_png.png");
  check(
    scale_bytes.size() > 26 && scale_bytes[25] == 4,
    "and it carries an alpha channel (IHDR colour type 4)");

  nav_msgs::msg::OccupancyGrid scale_loaded;
  check(
    nav2_map_server::loadMapFromYaml(dir + "/scale_png.yaml", scale_loaded) ==
    nav2_map_server::LOAD_MAP_SUCCESS,
    "loadMapFromYaml() reads the scale-mode PNG back");
  check(
    sameData(scale_loaded, scaled),
    "the transparent cells come back as unknown: " + describeData(scale_loaded));

  rclcpp::shutdown();
  std::printf("\n%s (%d failure(s))\n", failures == 0 ? "PASSED" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}
