// Reads one image through the shim's Magick++.h and prints what it decoded.
//
// Built by hand next to png_check.exe. The point is to feed the shim's *reader*
// files it did not write - Pillow's PNGs, and PNGs whose rows use each of the
// five filters - and to see the same pixels come out.
//
//     png_read.exe PATH
//     OK <width> <height> <has_alpha>
//     <gray bytes as hex>
//     <alpha bytes as hex, only when has_alpha>
#include "Magick++.h"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char ** argv)
{
  if (argc < 2) {
    std::printf("usage: png_read PATH\n");
    return 2;
  }
  try {
    Magick::Image img(argv[1]);
    const size_t width = img.columns();
    const size_t height = img.rows();
    std::vector<unsigned char> gray(width * height, 0);
    std::vector<unsigned char> alpha;
    img.write(0, 0, width, height, "I", Magick::CharPixel, gray.data());
    const bool has_alpha = img.matte();
    if (has_alpha) {
      alpha.assign(width * height, 0);
      img.write(0, 0, width, height, "A", Magick::CharPixel, alpha.data());
    }
    std::printf("OK %zu %zu %d\n", width, height, has_alpha ? 1 : 0);
    for (unsigned char value : gray) {
      std::printf("%02x", value);
    }
    std::printf("\n");
    if (has_alpha) {
      for (unsigned char value : alpha) {
        std::printf("%02x", value);
      }
      std::printf("\n");
    }
    return 0;
  } catch (const std::exception & e) {
    std::printf("ERR %s\n", e.what());
    return 1;
  }
}