#ifndef MAGICK__H_
#define MAGICK__H_

// Minimal stand-in for GraphicsMagick/ImageMagick's Magick++ headers, covering
// exactly what `nav2_map_server` uses and nothing else.
//
// Upstream's `nav2_map_server/src/map_io.cpp` reads and writes map images
// through Magick++, which would mean shipping GraphicsMagick (`CORE_RL_*.dll`,
// ~10 MB, plus Magick++'s own ABI) on a port whose whole point is that the
// binaries are self-contained. The file is the *only* place in navigation2 that
// touches an image library, and what it asks of it is small: load a file, get
// the pixels out as 8-bit gray, set pixels one at a time, write a file.
//
// A ROS2 map is a PGM and a YAML sidecar in practice - the format `map_saver`
// picks by itself (`checkSaveParameters()` defaults to "pgm" for trinary mode)
// and the format nav2's own maps and test assets ship as. So this header
// implements PGM: P5 (binary) and P2 (ascii) on the way in, P5 on the way out.
//
// It also implements PNG, because PGM cannot express the one thing a ROS2 map
// sometimes needs - transparency. `mode: scale` writes an alpha channel, which
// is exactly why `checkSaveParameters()` defaults `image_format` to "png" in
// that mode, and slam_toolbox's own configuration names `image_format: png`
// too. That coder is 8 bits, matching this Q8 build: grayscale, RGB, RGBA,
// gray+alpha and palette on the way in, grayscale or grayscale+alpha on the
// way out. Anything else - bmp, jpeg, an interlaced or 16-bit PNG - throws,
// and the message says which one it was.
//
// The subset is the one upstream calls, with upstream's semantics:
//
//     Magick::InitializeMagick(nullptr);          // no-op
//     Magick::Image img(path);                    // load, throws on failure
//     img.size().width(); img.columns(); img.rows(); img.matte();
//     Magick::Image gray = img; gray.type(Magick::GrayscaleType);
//     gray.write(0, 0, w, h, "I", Magick::CharPixel, buffer);   // read pixels
//     img.write(0, 0, w, h, "A", Magick::CharPixel, buffer);    // read alpha
//     Magick::Image out({w, h}, "red"); out.type(...); out.depth(8);
//     Magick::Color pixel = Magick::ColorGray(205 / 255.0);
//     pixel.alphaQuantum(TransparentOpacity);
//     out.pixelColor(x, y, pixel); out.write(path);
//     Magick::CoderInfo info("pgm"); info.isWritable();
//
// Two things are deliberately not like Magick++:
//
// * `Quantum` is 8 bits (`MaxRGB` = 255), i.e. a Q8 build - and the PNG coder
//   is 8 bits to match. The arithmetic in map_io.cpp is written against
//   `MaxRGB` and `ColorGray(0..1)`, so it lands on the same gray levels either
//   way - it asks for `205 / 255.0`, not 205.
// * There is no alpha channel in a PGM, so a PGM round trip drops one - which
//   is exactly what real Magick++ does. PNG carries it, which is why
//   `mode: scale` reaches for png in the first place.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// The PNG coder's decompressor. zlib ships with the MSYS2 UCRT64 toolchain
// that builds this (libz.a, linked statically like everything else here), so
// it costs no vendored source and no DLL to put next to the binaries. It is
// included out here because it is a C header.
#include <zlib.h>

namespace Magick
{

// Q8: see the header comment. `MaxRGB` is the white point, as in Magick++.
using Quantum = unsigned char;
constexpr Quantum MaxRGB = 255;

// Alpha is stored as *opacity*, the way Magick++ stores it: 0 is fully
// transparent, MaxRGB opaque. `Color::alphaQuantum()` takes the same scale.
constexpr Quantum OpaqueOpacity = MaxRGB;
constexpr Quantum TransparentOpacity = 0;

enum ImageType
{
  UndefinedType,
  GrayscaleType,
  GrayscaleMatteType,
  TrueColorType,
  TrueColorMatteType
};

// Magick++'s pixel storage types. Only CharPixel is implemented; the others are
// named so that a caller asking for one gets a clear error instead of a link
// error.
enum StorageType
{
  CharPixel,
  ShortPixel,
  IntegerPixel,
  FloatPixel,
  DoublePixel
};

class Exception : public std::exception
{
public:
  explicit Exception(const std::string & reason)
  : reason_(reason) {}

  const char * what() const noexcept override {return reason_.c_str();}

private:
  std::string reason_;
};

// Magick++'s exception hierarchy, in the two shapes map_io.cpp catches:
// `ErrorOption` for "asked for something this build cannot do" (upstream
// catches it around `CoderInfo` and falls back) and the rest for I/O and
// decoding failures.
class ErrorOption : public Exception
{
public:
  using Exception::Exception;
};

class ErrorMissingDelegate : public Exception
{
public:
  using Exception::Exception;
};

class ErrorCorruptImage : public Exception
{
public:
  using Exception::Exception;
};

class Geometry
{
public:
  Geometry() = default;
  Geometry(size_t width, size_t height)
  : width_(width), height_(height) {}

  size_t width() const {return width_;}
  size_t height() const {return height_;}

private:
  size_t width_ = 0;
  size_t height_ = 0;
};

class Color
{
public:
  Color() = default;
  Color(Quantum red, Quantum green, Quantum blue, Quantum alpha = OpaqueOpacity)
  : red_(red), green_(green), blue_(blue), alpha_(alpha) {}

  void alphaQuantum(Quantum alpha) {alpha_ = alpha;}
  Quantum alphaQuantum() const {return alpha_;}
  Quantum redQuantum() const {return red_;}
  Quantum greenQuantum() const {return green_;}
  Quantum blueQuantum() const {return blue_;}

private:
  Quantum red_ = 0;
  Quantum green_ = 0;
  Quantum blue_ = 0;
  Quantum alpha_ = OpaqueOpacity;
};

// `ColorGray(205 / 255.0)` and `ColorGray{0.5}` are the two spellings upstream
// uses; both mean "this gray level", on a scale of 0 to 1.
class ColorGray : public Color
{
public:
  explicit ColorGray(double gray)
  : Color(fromUnit(gray), fromUnit(gray), fromUnit(gray)) {}

  explicit ColorGray(Quantum gray)
  : Color(gray, gray, gray) {}

  static Quantum fromUnit(double gray)
  {
    const double clamped = std::min(1.0, std::max(0.0, gray));
    return static_cast<Quantum>(std::lround(clamped * MaxRGB));
  }
};

namespace detail
{

inline std::string lowered(const std::string & text)
{
  std::string result = text;
  std::transform(
    result.begin(), result.end(), result.begin(),
    [](unsigned char c) {return static_cast<char>(std::tolower(c));});
  return result;
}

// The extension, without the dot, lowercased. "" when there is none.
inline std::string extensionOf(const std::string & path)
{
  const size_t dot = path.find_last_of('.');
  const size_t separator = path.find_last_of("/\\");
  if (dot == std::string::npos || (separator != std::string::npos && dot < separator)) {
    return "";
  }
  return lowered(path.substr(dot + 1));
}

// PGM headers are whitespace separated and may carry `#` comments anywhere a
// token could start.
template<class StreamT>
bool nextToken(StreamT & stream, std::string & token)
{
  token.clear();
  int c = stream.get();
  for (;;) {
    while (c != EOF && std::isspace(c)) {
      c = stream.get();
    }
    if (c == '#') {
      while (c != EOF && c != '\n') {
        c = stream.get();
      }
      continue;
    }
    break;
  }
  if (c == EOF) {
    return false;
  }
  while (c != EOF && !std::isspace(c)) {
    token.push_back(static_cast<char>(c));
    c = stream.get();
  }
  return !token.empty();
}

// ------------------------------------------------------------------ PNG -----
//
// The PNG coder: 8-bit grayscale in and out, plus the alpha channel `mode:
// scale` needs. Upstream reaches for png exactly when the mode is `scale`
// (`checkSaveParameters()` in map_io.cpp defaults `image_format` to "png"
// there, because scale is the only mode that writes transparency), and
// slam_toolbox's own default configuration names `image_format: png`. What
// follows is the container - chunks, CRCs, the five row filters, and the
// colour types a map written by another tool can arrive in. The compression is
// zlib's, which the toolchain already carries.
//
// Deliberately not decoded, each refused by name rather than guessed at:
// interlaced files, bit depths other than 8, and colour types other than the
// five below. Chunks that do not affect pixels (gAMA, pHYs, tEXt, ...) are
// skipped, which is what a reader is supposed to do with an unknown ancillary
// chunk.

inline const unsigned char * pngSignature()
{
  static const unsigned char signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  return signature;
}

inline bool isPng(const std::vector<unsigned char> & bytes)
{
  return bytes.size() >= 8 && std::equal(bytes.begin(), bytes.begin() + 8, pngSignature());
}

// The whole file, which is what both readers here want: a map image is small
// enough to hold, and the PNG reader has to walk it from the end of the
// signature anyway.
inline std::vector<unsigned char> readWholeFile(const std::string & path)
{
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw ErrorMissingDelegate("cannot open image '" + path + "' (it is not there)");
  }
  std::vector<unsigned char> bytes(
    (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  return bytes;
}

inline std::uint32_t readBe32(const unsigned char * p)
{
  return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

inline void putBe32(std::vector<unsigned char> & out, std::uint32_t value)
{
  out.push_back(static_cast<unsigned char>((value >> 24) & 0xFF));
  out.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
  out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
  out.push_back(static_cast<unsigned char>(value & 0xFF));
}

// zlib's crc32 takes a 32-bit length, and a map's IDAT is bigger than that, so
// anything long is walked in pieces rather than truncated.
inline std::uint32_t crc32Of(const unsigned char * data, std::size_t length)
{
  ::uLong crc = ::crc32(0L, Z_NULL, 0);
  while (length > 0) {
    const std::size_t piece = std::min<std::size_t>(length, std::size_t{1} << 24);
    crc = ::crc32(crc, data, static_cast<::uInt>(piece));
    data += piece;
    length -= piece;
  }
  return static_cast<std::uint32_t>(crc);
}

inline void appendChunk(
  std::vector<unsigned char> & out, const char * type, const std::vector<unsigned char> & payload)
{
  putBe32(out, static_cast<std::uint32_t>(payload.size()));
  const std::size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), payload.begin(), payload.end());
  putBe32(out, crc32Of(out.data() + start, 4 + payload.size()));
}

inline std::vector<unsigned char> zlibCompress(const std::vector<unsigned char> & raw)
{
  ::uLongf bound = ::compressBound(static_cast<::uLong>(raw.size()));
  std::vector<unsigned char> out(static_cast<std::size_t>(bound));
  const int status = ::compress2(
    out.data(), &bound, raw.data(), static_cast<::uLong>(raw.size()), 6);
  if (status != Z_OK) {
    throw ErrorCorruptImage("zlib could not compress the image (status " +
            std::to_string(status) + ")");
  }
  out.resize(static_cast<std::size_t>(bound));
  return out;
}

// Rec. 601 luma, rounded. A grayscale file is unaffected (r = g = b), and an
// RGB one lands where a reader would expect it to.
inline Quantum luma(unsigned char red, unsigned char green, unsigned char blue)
{
  return static_cast<Quantum>((299u * red + 587u * green + 114u * blue + 500u) / 1000u);
}

inline int paethPredictor(int left, int up, int upleft)
{
  const int estimate = left + up - upleft;
  const int to_left = std::abs(estimate - left);
  const int to_up = std::abs(estimate - up);
  const int to_upleft = std::abs(estimate - upleft);
  if (to_left <= to_up && to_left <= to_upleft) {
    return left;
  }
  return (to_up <= to_upleft) ? up : upleft;
}

// Decodes `bytes` into `gray` (and `alpha`, empty when the file has none).
inline void decodePng(
  const std::vector<unsigned char> & bytes, const std::string & path, std::size_t & width,
  std::size_t & height, std::vector<Quantum> & gray, std::vector<Quantum> & alpha)
{
  std::uint32_t file_width = 0;
  std::uint32_t file_height = 0;
  int depth = 0;
  int color_type = -1;
  int interlace = 0;
  bool have_header = false;
  std::vector<unsigned char> palette;
  std::vector<unsigned char> transparency;
  std::vector<unsigned char> idat;

  std::size_t offset = 8;
  while (offset + 8 <= bytes.size()) {
    const std::uint32_t length = readBe32(&bytes[offset]);
    const std::string type(reinterpret_cast<const char *>(&bytes[offset + 4]), 4);
    const std::size_t payload = offset + 8;
    if (payload + length + 4 > bytes.size()) {
      throw ErrorCorruptImage("image '" + path + "' ends inside its '" + type + "' chunk");
    }
    if (type == "IHDR") {
      if (length < 13) {
        throw ErrorCorruptImage("image '" + path + "' has an IHDR shorter than 13 bytes");
      }
      file_width = readBe32(&bytes[payload]);
      file_height = readBe32(&bytes[payload + 4]);
      depth = bytes[payload + 8];
      color_type = bytes[payload + 9];
      interlace = bytes[payload + 12];
      have_header = true;
    } else if (type == "PLTE") {
      palette.assign(bytes.begin() + payload, bytes.begin() + payload + length);
    } else if (type == "tRNS") {
      transparency.assign(bytes.begin() + payload, bytes.begin() + payload + length);
    } else if (type == "IDAT") {
      idat.insert(idat.end(), bytes.begin() + payload, bytes.begin() + payload + length);
    } else if (type == "IEND") {
      break;
    }
    offset = payload + length + 4;
  }

  if (!have_header) {
    throw ErrorCorruptImage("image '" + path + "' is a PNG with no IHDR chunk");
  }
  if (file_width == 0 || file_height == 0) {
    throw ErrorCorruptImage(
            "image '" + path + "' declares " + std::to_string(file_width) + "x" +
            std::to_string(file_height));
  }
  if (interlace != 0) {
    throw ErrorMissingDelegate(
            "cannot read '" + path +
            "': interlaced PNG is not one of the two this build's Magick++ decodes "
            "(non-interlaced, 8 bits per channel)");
  }
  if (depth != 8) {
    throw ErrorMissingDelegate(
            "cannot read '" + path + "': this build's Magick++ decodes 8-bit PNG, and this "
            "file is " + std::to_string(depth) + "-bit");
  }

  std::size_t channels = 0;
  switch (color_type) {
    case 0: channels = 1; break;   // gray
    case 2: channels = 3; break;   // rgb
    case 3: channels = 1; break;   // palette
    case 4: channels = 2; break;   // gray + alpha
    case 6: channels = 4; break;   // rgba
    default:
      throw ErrorMissingDelegate(
              "cannot read '" + path + "': PNG colour type " + std::to_string(color_type) +
              " is not one of the five this build's Magick++ decodes");
  }
  if (color_type == 3 && palette.size() < 3) {
    throw ErrorCorruptImage("image '" + path + "' is paletted and carries no PLTE chunk");
  }

  const std::size_t row_bytes = static_cast<std::size_t>(file_width) * channels;
  const std::size_t expected = (row_bytes + 1) * static_cast<std::size_t>(file_height);
  std::vector<unsigned char> raw(expected);
  ::uLongf produced = static_cast<::uLongf>(expected);
  const int status = ::uncompress(
    raw.data(), &produced, idat.data(), static_cast<::uLong>(idat.size()));
  if (status != Z_OK || produced != expected) {
    throw ErrorCorruptImage(
            "image '" + path +
            "': the IDAT stream does not inflate to the " + std::to_string(expected) +
            " bytes an image of that size needs (zlib status " + std::to_string(status) + ")");
  }

  width = file_width;
  height = file_height;
  const std::size_t pixels = width * height;
  // `matte()` is what map_io.cpp reads to decide whether to mask transparent
  // cells, so the channel exists only where the file really carries one. A
  // colour-keyed file (types 0 and 2 with tRNS) keeps none: no map tool writes
  // one, and guessing at the key would be worse than not translating it.
  const bool file_has_alpha = (color_type == 4 || color_type == 6 ||
    (color_type == 3 && !transparency.empty()));
  gray.assign(pixels, 0);
  alpha.assign(file_has_alpha ? pixels : 0, OpaqueOpacity);

  std::vector<unsigned char> previous(row_bytes, 0);
  std::vector<unsigned char> current(row_bytes, 0);
  for (std::size_t y = 0; y < height; ++y) {
    const unsigned char * line = &raw[y * (row_bytes + 1)];
    const unsigned char filter = line[0];
    std::copy(line + 1, line + 1 + row_bytes, current.begin());
    switch (filter) {
      case 0:  // None
        break;
      case 1:  // Sub
        for (std::size_t i = channels; i < row_bytes; ++i) {
          current[i] = static_cast<unsigned char>(current[i] + current[i - channels]);
        }
        break;
      case 2:  // Up
        for (std::size_t i = 0; i < row_bytes; ++i) {
          current[i] = static_cast<unsigned char>(current[i] + previous[i]);
        }
        break;
      case 3:  // Average
        for (std::size_t i = 0; i < row_bytes; ++i) {
          const int left = (i >= channels) ? current[i - channels] : 0;
          current[i] = static_cast<unsigned char>(
            current[i] + ((left + static_cast<int>(previous[i])) / 2));
        }
        break;
      case 4:  // Paeth
        for (std::size_t i = 0; i < row_bytes; ++i) {
          const int left = (i >= channels) ? current[i - channels] : 0;
          const int up = previous[i];
          const int upleft = (i >= channels) ? previous[i - channels] : 0;
          current[i] = static_cast<unsigned char>(
            current[i] + paethPredictor(left, up, upleft));
        }
        break;
      default:
        throw ErrorCorruptImage(
                "image '" + path + "' row " + std::to_string(y) + " uses filter " +
                std::to_string(filter));
    }

    unsigned char * gray_row = gray.data() + y * width;
    unsigned char * alpha_row = file_has_alpha ? alpha.data() + y * width : nullptr;
    for (std::size_t x = 0; x < width; ++x) {
      const unsigned char * pixel = &current[x * channels];
      unsigned char level = 0;
      unsigned char opacity = OpaqueOpacity;
      switch (color_type) {
        case 0:
          level = pixel[0];
          break;
        case 2:
          level = luma(pixel[0], pixel[1], pixel[2]);
          break;
        case 3: {
            const std::size_t index = pixel[0];
            if (index * 3 + 2 >= palette.size()) {
              throw ErrorCorruptImage(
                      "image '" + path + "' row " + std::to_string(y) + " uses palette entry " +
                      std::to_string(index) + ", and the PLTE chunk has " +
                      std::to_string(palette.size() / 3) + " entries");
            }
            level = luma(palette[index * 3], palette[index * 3 + 1], palette[index * 3 + 2]);
            if (index < transparency.size()) {
              opacity = transparency[index];
            }
            break;
          }
        case 4:
          level = pixel[0];
          opacity = pixel[1];
          break;
        default:  // 6
          level = luma(pixel[0], pixel[1], pixel[2]);
          opacity = pixel[3];
          break;
      }
      gray_row[x] = level;
      if (alpha_row) {
        alpha_row[x] = opacity;
      }
    }
    previous.swap(current);
  }
}

// Encodes `gray` (and `alpha`, when it is not empty) as a PNG of exactly those
// pixels: colour type 0, or 4 when there is an alpha channel.
//
// Every row uses filter 0. Filtering is a compression choice, not a format
// one - a conforming reader has to implement all five either way - and what
// this writes is an occupancy map, which is mostly long runs of one level
// that zlib's own LZ77 already collapses. Picking a filter per row would buy
// a few percent on files that are already small.
inline std::vector<unsigned char> encodePng(
  std::size_t width, std::size_t height, const std::vector<Quantum> & gray,
  const std::vector<Quantum> & alpha)
{
  if (gray.size() != width * height) {
    throw ErrorCorruptImage("encodePng(): the pixel buffer is not width * height");
  }
  const bool with_alpha = !alpha.empty();
  if (with_alpha && alpha.size() != gray.size()) {
    throw ErrorCorruptImage("encodePng(): the alpha channel is not the size of the image");
  }

  std::vector<unsigned char> out;
  out.insert(out.end(), pngSignature(), pngSignature() + 8);

  std::vector<unsigned char> ihdr;
  putBe32(ihdr, static_cast<std::uint32_t>(width));
  putBe32(ihdr, static_cast<std::uint32_t>(height));
  ihdr.push_back(8);                                        // bit depth
  ihdr.push_back(with_alpha ? 4 : 0);                       // colour type
  ihdr.push_back(0);                                        // deflate
  ihdr.push_back(0);                                        // adaptive filtering
  ihdr.push_back(0);                                        // no interlace
  appendChunk(out, "IHDR", ihdr);

  const std::size_t channels = with_alpha ? 2 : 1;
  std::vector<unsigned char> raw;
  raw.reserve((width * channels + 1) * height);
  for (std::size_t y = 0; y < height; ++y) {
    raw.push_back(0);  // filter 0, see above
    const unsigned char * gray_row = gray.data() + y * width;
    const unsigned char * alpha_row = with_alpha ? alpha.data() + y * width : nullptr;
    for (std::size_t x = 0; x < width; ++x) {
      raw.push_back(gray_row[x]);
      if (alpha_row) {
        raw.push_back(alpha_row[x]);
      }
    }
  }
  appendChunk(out, "IDAT", zlibCompress(raw));
  appendChunk(out, "IEND", std::vector<unsigned char>());
  return out;
}

}  // namespace detail

class Image
{
public:
  Image() = default;

  // Load from a file. Magick++ sniffs the format from the content; this one
  // accepts PNG and the two PGM magics, and rejects everything else by name.
  explicit Image(const std::string & path)
  {
    read(path);
  }

  // `Image({width, height}, "color")`: an image of one color, used by
  // map_io.cpp as a canvas whose initial pixels are a canary - every pixel is
  // overwritten before the image is written out.
  Image(std::initializer_list<size_t> size, const std::string & color)
  {
    if (size.size() != 2) {
      throw ErrorOption("Image({width, height}, color): expected two values");
    }
    const size_t width = *size.begin();
    const size_t height = *(size.begin() + 1);
    fill(width, height, color);
  }

  void read(const std::string & path)
  {
    // Both readers want the whole file, and the PNG one has to walk it from
    // the end of its signature anyway.
    const std::vector<unsigned char> bytes = detail::readWholeFile(path);
    if (detail::isPng(bytes)) {
      detail::decodePng(bytes, path, width_, height_, gray_, alpha_);
      type_ = alpha_.empty() ? GrayscaleType : GrayscaleMatteType;
      return;
    }

    // Not a PNG, so it has to be a PGM: whitespace separated header tokens,
    // with `#` comments allowed anywhere a token could start.
    std::istringstream stream(std::string(bytes.begin(), bytes.end()));
    std::string magic;
    std::string width_text;
    std::string height_text;
    std::string maxval_text;
    if (!detail::nextToken(stream, magic) || (magic != "P5" && magic != "P2")) {
      throw ErrorMissingDelegate(
              "image '" + path + "' is neither a PNG nor a PGM (magic '" + magic +
              "'; this build's Magick++ reads 8-bit PNG, P5 and P2)");
    }
    const bool ascii = (magic == "P2");
    if (!detail::nextToken(stream, width_text) || !detail::nextToken(stream, height_text) ||
      !detail::nextToken(stream, maxval_text))
    {
      throw ErrorCorruptImage("image '" + path + "' has a truncated PGM header");
    }

    const long width = std::stol(width_text);
    const long height = std::stol(height_text);
    const long maxval = std::stol(maxval_text);
    if (width <= 0 || height <= 0 || maxval <= 0 || maxval > 65535) {
      throw ErrorCorruptImage(
              "image '" + path + "' declares " + width_text + "x" + height_text +
              " at maxval " + maxval_text);
    }

    std::vector<Quantum> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
    // PGM's white point is `maxval`, which is not required to be 255 - a 15-bit
    // map from another tool has to land on the same gray levels as a 8-bit one.
    const auto toQuantum = [maxval](long value) {
        return static_cast<Quantum>(std::lround(
                 static_cast<double>(value) * MaxRGB / static_cast<double>(maxval)));
      };

    if (ascii) {
      for (size_t i = 0; i < pixels.size(); ++i) {
        std::string value_text;
        if (!detail::nextToken(stream, value_text)) {
          throw ErrorCorruptImage("image '" + path + "' ends before pixel " + std::to_string(i));
        }
        pixels[i] = toQuantum(std::stol(value_text));
      }
    } else if (maxval < 256) {
      // One byte per pixel, but at most `maxval`.
      std::vector<char> raw(pixels.size());
      if (!stream.read(raw.data(), static_cast<std::streamsize>(raw.size()))) {
        throw ErrorCorruptImage("image '" + path + "' ends before its last row");
      }
      for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = toQuantum(static_cast<unsigned char>(raw[i]));
      }
    } else {
      // Two bytes per pixel, most significant first.
      std::vector<char> raw(pixels.size() * 2);
      if (!stream.read(raw.data(), static_cast<std::streamsize>(raw.size()))) {
        throw ErrorCorruptImage("image '" + path + "' ends before its last row");
      }
      for (size_t i = 0; i < pixels.size(); ++i) {
        const long value = (static_cast<unsigned char>(raw[i * 2]) << 8) |
          static_cast<unsigned char>(raw[i * 2 + 1]);
        pixels[i] = toQuantum(value);
      }
    }

    width_ = static_cast<size_t>(width);
    height_ = static_cast<size_t>(height);
    gray_.swap(pixels);
    alpha_.clear();
    type_ = GrayscaleType;
  }

  void write(const std::string & path) const
  {
    const std::string extension = detail::extensionOf(path);
    const bool as_png = (extension == "png");
    if (!as_png && extension != "pgm" && extension != "pnm") {
      throw ErrorMissingDelegate(
              "cannot encode '" + path +
              "': this build's Magick++ writes PGM and PNG only (the shim ports "
              "nav2_map_server without GraphicsMagick - use image_format 'pgm' or 'png')");
    }
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
      throw ErrorCorruptImage("cannot open '" + path + "' for writing");
    }
    if (as_png) {
      // The alpha channel goes out with it when the caller made one, which is
      // exactly what `mode: scale` does - and why map_saver asks for png there.
      const std::vector<unsigned char> bytes =
        detail::encodePng(width_, height_, gray_, alpha_);
      stream.write(
        reinterpret_cast<const char *>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    } else {
      // A PGM has no alpha channel, so writing an image that has one drops it -
      // which is what real Magick++ does too.
      stream << "P5\n" << width_ << " " << height_ << "\n" << static_cast<int>(MaxRGB) << "\n";
      stream.write(
        reinterpret_cast<const char *>(gray_.data()),
        static_cast<std::streamsize>(gray_.size()));
    }
    if (!stream) {
      throw ErrorCorruptImage("failed while writing '" + path + "'");
    }
  }

  // `write(x, y, width, height, "I", CharPixel, buffer)`: copy pixels *out* of
  // the image into the caller's buffer. Magick++ calls this "write"; from the
  // buffer's point of view it is a read.
  void write(
    long x, long y, size_t width, size_t height, const std::string & map, StorageType storage,
    void * pixels) const
  {
    if (storage != CharPixel) {
      throw ErrorOption("only CharPixel is implemented");
    }
    const bool intensity = (map == "I");
    const bool matte = (map == "A");
    if (!intensity && !matte) {
      throw ErrorOption("unsupported channel map '" + map + "'");
    }
    unsigned char * out = static_cast<unsigned char *>(pixels);
    for (size_t row = 0; row < height; ++row) {
      for (size_t column = 0; column < width; ++column) {
        const size_t source_x = static_cast<size_t>(x) + column;
        const size_t source_y = static_cast<size_t>(y) + row;
        if (x < 0 || y < 0 || source_x >= width_ || source_y >= height_) {
          throw ErrorOption("requested region is outside the image");
        }
        out[row * width + column] = intensity ?
          gray_[source_y * width_ + source_x] :
          (alpha_.empty() ? OpaqueOpacity : alpha_[source_y * width_ + source_x]);
      }
    }
  }

  void pixelColor(size_t x, size_t y, const Color & color)
  {
    if (x >= width_ || y >= height_) {
      throw ErrorOption("pixelColor() outside the image");
    }
    const size_t index = y * width_ + x;
    gray_[index] = color.redQuantum();
    if (!alpha_.empty()) {
      alpha_[index] = color.alphaQuantum();
    }
  }

  void type(ImageType value)
  {
    type_ = value;
    // `map_io.cpp` asks for TrueColorMatteType exactly when it is about to use
    // the alpha channel, so that call is what creates the channel here.
    const bool wants_matte = (value == TrueColorMatteType || value == GrayscaleMatteType);
    if (wants_matte && alpha_.size() != gray_.size()) {
      alpha_.assign(gray_.size(), OpaqueOpacity);
    } else if (!wants_matte) {
      alpha_.clear();
    }
  }

  void depth(size_t) {}  // Q8 is the only depth here

  size_t columns() const {return width_;}
  size_t rows() const {return height_;}
  Geometry size() const {return Geometry(width_, height_);}
  bool matte() const {return !alpha_.empty();}
  ImageType type() const {return type_;}

private:
  void fill(size_t width, size_t height, const std::string & color)
  {
    if (width == 0 || height == 0) {
      throw ErrorOption("Image({width, height}, color): a zero-sized image");
    }
    const std::string name = detail::lowered(color);
    // Only "black" is distinguished; anything else is a canary fill that is
    // overwritten before the image is written out (see the class comment), so
    // the other names Magick++ would accept all land on white here.
    const Quantum level = (name == "black") ? 0 : MaxRGB;
    width_ = width;
    height_ = height;
    gray_.assign(width * height, level);
    alpha_.clear();
    type_ = (name == "black" || name == "white" || name == "red") ? GrayscaleType : UndefinedType;
  }

  size_t width_ = 0;
  size_t height_ = 0;
  ImageType type_ = UndefinedType;
  std::vector<Quantum> gray_;
  std::vector<Quantum> alpha_;
};

// Magick++ asks the coder registry whether a format can be read or written; a
// format this build has no coder for throws, which is what upstream's
// `checkSaveParameters()` catches to fall back. The fallback is "png", and this
// build now really has that coder: asking for bmp gets the same warning a build
// without that delegate gives and a png comes out, instead of a map that only
// fails at the last moment.
class CoderInfo
{
public:
  explicit CoderInfo(const std::string & name)
  : name_(detail::lowered(name))
  {
    if (name_ != "pgm" && name_ != "png") {
      throw ErrorOption(
              "no coder for '" + name + "': this build's Magick++ speaks pgm and png only");
    }
  }

  bool isWritable() const {return name_ == "pgm" || name_ == "png";}
  bool isReadable() const {return name_ == "pgm" || name_ == "png";}
  bool isValid() const {return name_ == "pgm" || name_ == "png";}

private:
  std::string name_;
};

inline void InitializeMagick(const char *) {}

}  // namespace Magick

// Real Magick++ gets these three from C headers, where they are macros, so
// upstream's `map_io.cpp` uses them unqualified (`q = MaxRGB;`,
// `pixel.alphaQuantum(TransparentOpacity);`). The aliases keep both spellings
// working without wrapping the names in macros, which would break
// `Magick::MaxRGB` for everyone else.
using Magick::MaxRGB;
using Magick::OpaqueOpacity;
using Magick::TransparentOpacity;

#endif  // MAGICK__H_
