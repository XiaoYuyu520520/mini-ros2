// Standalone check of the shim's Magick++.h PNG coder.
//
// Built and run by hand (not a ctest) so the coder can be exercised without
// waiting on the whole shim link:
//
//     g++ -std=c++20 -static -I nav2/ros2_shim/include .scratch/png_check.cpp -lz
//
// Everything it writes is also handed to .scratch/png_verify.py, which parses
// the chunks and checks the CRCs with Python's own zlib - an encoder and a
// decoder that share no code with this one.
#include "Magick++.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

std::vector<unsigned char> readAll(const std::string & path)
{
  std::ifstream stream(path, std::ios::binary);
  return std::vector<unsigned char>(
    (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

const unsigned char kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

}  // namespace

int main(int argc, char ** argv)
{
  const std::string dir = (argc > 1) ? argv[1] : ".";

  // ---- 1. 8-bit grayscale, no alpha --------------------------------------
  const std::vector<unsigned char> levels{
    254, 254, 254, 254,
    205, 205, 205, 205,
    0, 0, 0, 0};
  Magick::Image gray({4, 3}, "red");
  gray.type(Magick::GrayscaleType);
  gray.depth(8);
  for (size_t y = 0; y < 3; ++y) {
    for (size_t x = 0; x < 4; ++x) {
      gray.pixelColor(x, y, Magick::ColorGray(levels[y * 4 + x] / 255.0));
    }
  }
  gray.write(dir + "/gray.png");

  const std::vector<unsigned char> gray_bytes = readAll(dir + "/gray.png");
  check(
    gray_bytes.size() > 26 && std::equal(gray_bytes.begin(), gray_bytes.begin() + 8, kSignature),
    "gray.png starts with the PNG signature");
  check(
    gray_bytes.size() > 26 && gray_bytes[24] == 8 && gray_bytes[25] == 0,
    "gray.png is 8-bit grayscale (IHDR depth 8, colour type 0)");
  check(
    gray_bytes.size() > 28 && gray_bytes[26] == 0 && gray_bytes[27] == 0 && gray_bytes[28] == 0,
    "gray.png is deflate / adaptive-filter / non-interlaced");

  Magick::Image gray_back(dir + "/gray.png");
  check(!gray_back.matte(), "gray.png reads back with no alpha channel");
  check(gray_back.columns() == 4 && gray_back.rows() == 3, "gray.png reads back 4x3");
  std::vector<unsigned char> gray_pixels(12, 0);
  gray_back.write(0, 0, 4, 3, "I", Magick::CharPixel, gray_pixels.data());
  check(gray_pixels == levels, "the gray levels survive the round trip");

  // ---- 2. the same pixels through the PGM coder ---------------------------
  gray.write(dir + "/gray.pgm");
  Magick::Image pgm_back(dir + "/gray.pgm");
  std::vector<unsigned char> pgm_pixels(12, 0);
  pgm_back.write(0, 0, 4, 3, "I", Magick::CharPixel, pgm_pixels.data());
  check(pgm_pixels == levels, "the PGM coder still round trips");

  // ---- 3. grayscale + alpha ---------------------------------------------
  const std::vector<unsigned char> alpha_levels{0, 255, 128, 255};
  const std::vector<unsigned char> alpha_values{255, 255, 0, 255};
  Magick::Image matte({2, 2}, "white");
  matte.type(Magick::TrueColorMatteType);
  matte.depth(8);
  for (size_t y = 0; y < 2; ++y) {
    for (size_t x = 0; x < 2; ++x) {
      Magick::Color pixel = Magick::ColorGray(alpha_levels[y * 2 + x] / 255.0);
      pixel.alphaQuantum(alpha_values[y * 2 + x]);
      matte.pixelColor(x, y, pixel);
    }
  }
  matte.write(dir + "/matte.png");

  const std::vector<unsigned char> matte_bytes = readAll(dir + "/matte.png");
  check(
    matte_bytes.size() > 26 && matte_bytes[25] == 4,
    "matte.png is gray + alpha (IHDR colour type 4)");

  Magick::Image matte_back(dir + "/matte.png");
  check(matte_back.matte(), "matte.png reads back with an alpha channel");
  std::vector<unsigned char> matte_gray(4, 0);
  std::vector<unsigned char> matte_alpha(4, 0);
  matte_back.write(0, 0, 2, 2, "I", Magick::CharPixel, matte_gray.data());
  matte_back.write(0, 0, 2, 2, "A", Magick::CharPixel, matte_alpha.data());
  check(matte_gray == alpha_levels, "the alpha image's gray levels survive");
  check(matte_alpha == alpha_values, "the alpha channel survives");

  // ---- 4. what it refuses -------------------------------------------------
  {
    std::ofstream out(dir + "/truncated.png", std::ios::binary);
    out.write(reinterpret_cast<const char *>(kSignature), 8);
  }
  bool threw = false;
  try {
    Magick::Image bad(dir + "/truncated.png");
  } catch (const Magick::Exception &) {
    threw = true;
  }
  check(threw, "a PNG that stops after its signature throws");

  threw = false;
  try {
    Magick::Image bad(dir + "/absent.pgm");
  } catch (const Magick::Exception &) {
    threw = true;
  }
  check(threw, "a file that is not there throws");

  bool coder_threw = false;
  try {
    Magick::CoderInfo info("bmp");
  } catch (const Magick::ErrorOption &) {
    coder_threw = true;
  }
  check(coder_threw, "CoderInfo('bmp') throws, so map_io falls back to png");
  check(Magick::CoderInfo("png").isWritable(), "CoderInfo('png') is writable");
  check(Magick::CoderInfo("pgm").isWritable(), "CoderInfo('pgm') is writable");

  std::printf("\n%s (%d failure(s))\n", failures == 0 ? "PASSED" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}