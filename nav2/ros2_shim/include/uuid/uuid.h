#ifndef SHIM_UUID__UUID_H_
#define SHIM_UUID__UUID_H_

// libuuid's interface, for the one nav2 package that uses it.
//
// nav2_map_server's vector-object server keeps every shape under a UUID, and
// `vector_object_shapes.cpp` / `vector_object_utils.hpp` reach for the system
// libuuid to make, print, compare and parse them. MSYS2 ships no libuuid, so
// this is the part of it those files call and nothing more.
//
//   here       uuid_t and the nine functions the port needs
//   not here   the rest of libuuid - `uuid_time`, `uuid_type`, `uuid_variant`,
//              the on-disk clock sequence, `uuid_generate_time_safe`. Nothing
//              in the port calls them and none of them is stubbed out to
//              answer wrongly: a caller that wants one fails to compile.
//
// Byte order is libuuid's, not Windows': `uuid_t` is the RFC 4122 wire order
// (big-endian for the first three fields), which is what `uuid_parse` and
// `uuid_unparse` below are written against. The two are exact inverses, so a
// UUID survives the trip through a parameters file and back unchanged.
//
// `uuid_generate` does *not* ask the operating system for a UUID. Windows'
// GUID service is `CoCreateGuid` in ole32, and reaching it from here means
// either including <objbase.h> - which defines `interface`, `small` and
// `hyper` as macros and would corrupt the nav2 headers this file sits next to -
// or redeclaring it, which collides if any other translation unit has included
// the real declaration. So the generator is self-contained: 128 bits from a
// seeded `std::random_device`, with the version and variant bits set the way
// RFC 4122 section 4.4 requires. That is enough for what these UUIDs are for -
// identifying a shape across the wire and across a restart - and it is not a
// security token; if a caller ever needs one, that is the moment to link the
// real thing rather than to reuse this.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>

/// libuuid's `uuid_t`: sixteen bytes, RFC 4122 order.
typedef unsigned char uuid_t[16];

namespace shim_uuid_detail
{

inline int hexValue(char c)
{
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

inline std::uint64_t randomBits()
{
  static std::mt19937_64 engine(std::random_device{}());
  return engine();
}

}  // namespace shim_uuid_detail

/// Reads the 32 hex digits of a canonical 8-4-4-4-12 spelling, ignoring the
/// separators wherever they fall. libuuid stops at the thirty-sixth character
/// and accepts anything after it; so does this, and it also accepts a spelling
/// whose separators are not dashes, because nothing here depends on them.
inline int uuid_parse(const char * in, uuid_t uu)
{
  if (in == nullptr) {
    return -1;
  }
  unsigned char bytes[16];
  int count = 0;
  int high = -1;
  for (const char * p = in; *p != '\0'; ++p) {
    const int value = shim_uuid_detail::hexValue(*p);
    if (value < 0) {
      continue;
    }
    if (high < 0) {
      high = value;
      continue;
    }
    if (count >= 16) {
      // A thirty-third hex digit: not one UUID, so nothing to read.
      return -1;
    }
    bytes[count++] = static_cast<unsigned char>((high << 4) | value);
    high = -1;
  }
  if (high >= 0 || count != 16) {
    return -1;
  }
  std::memcpy(uu, bytes, 16);
  return 0;
}

inline void uuid_unparse_lower(const uuid_t uu, char * out)
{
  static const char * hex = "0123456789abcdef";
  std::size_t at = 0;
  for (std::size_t i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      out[at++] = '-';
    }
    out[at++] = hex[(uu[i] >> 4) & 0x0f];
    out[at++] = hex[uu[i] & 0x0f];
  }
  out[at] = '\0';
}

inline void uuid_unparse_upper(const uuid_t uu, char * out)
{
  uuid_unparse_lower(uu, out);
  for (char * p = out; *p != '\0'; ++p) {
    if (*p >= 'a' && *p <= 'f') {
      *p = static_cast<char>(*p - 'a' + 'A');
    }
  }
}

/// libuuid's `uuid_unparse` writes the lower-case form.
inline void uuid_unparse(const uuid_t uu, char * out)
{
  uuid_unparse_lower(uu, out);
}

inline void uuid_clear(uuid_t uu)
{
  std::memset(uu, 0, 16);
}

inline void uuid_copy(uuid_t dst, const uuid_t src)
{
  std::memcpy(dst, src, 16);
}

inline int uuid_compare(const uuid_t uu1, const uuid_t uu2)
{
  const int difference = std::memcmp(uu1, uu2, 16);
  return difference < 0 ? -1 : (difference > 0 ? 1 : 0);
}

inline int uuid_is_null(const uuid_t uu)
{
  for (std::size_t i = 0; i < 16; ++i) {
    if (uu[i] != 0) {
      return 0;
    }
  }
  return 1;
}

inline void uuid_generate(uuid_t out)
{
  std::uint64_t high = shim_uuid_detail::randomBits();
  std::uint64_t low = shim_uuid_detail::randomBits();
  for (std::size_t i = 0; i < 8; ++i) {
    out[i] = static_cast<unsigned char>((high >> (56 - 8 * i)) & 0xff);
    out[i + 8] = static_cast<unsigned char>((low >> (56 - 8 * i)) & 0xff);
  }
  // Version 4 (random) in the high nibble of byte 6, variant 1 (10xx) in the
  // top two bits of byte 8.
  out[6] = static_cast<unsigned char>((out[6] & 0x0f) | 0x40);
  out[8] = static_cast<unsigned char>((out[8] & 0x3f) | 0x80);
}

/// The random generator and the default one are the same here; the name exists
/// so that a caller written against libuuid compiles.
inline void uuid_generate_random(uuid_t out)
{
  uuid_generate(out);
}

#endif  // SHIM_UUID__UUID_H_
