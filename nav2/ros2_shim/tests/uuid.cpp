// The shim's <uuid/uuid.h>, which exists so that nav2_map_server's
// vector-object server compiles on a toolchain without libuuid.
//
// A UUID is an identifier: it is written into a parameters file, read back,
// compared against the one a message carried and printed for a human. Every one
// of those steps goes through one of these functions, so what is checked here
// is that they agree with each other and with the spelling every other tool
// uses:
//
//   1. `uuid_unparse` of a known UUID is the canonical 8-4-4-4-12 text,
//   2. `uuid_parse` of that text gives back the same sixteen bytes - in either
//      case, and with the separators ignored wherever they fall,
//   3. `uuid_compare` orders two UUIDs by their bytes, and a UUID compares
//      equal to a copy of itself,
//   4. `uuid_is_null` is true only for an all-zero UUID, and `uuid_clear` makes
//      one,
//   5. `uuid_generate` produces a version 4, variant 1 UUID that is not null
//      and that two calls do not repeat.
#include "uuid/uuid.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace
{

int failures = 0;

void expect(bool condition, const std::string & what)
{
  if (!condition) {
    std::printf("  FAIL %s\n", what.c_str());
    ++failures;
  }
}

void section(const char * name)
{
  std::printf("%s\n", name);
}

}  // namespace

int main()
{
  // RFC 4122's own example, which is also the one `uuidparse` in util-linux
  // prints, so "correct" is not this file's opinion of itself.
  static const unsigned char kKnown[16] = {
    0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
    0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
  static const char * kText = "12345678-9abc-def0-1234-56789abcdef0";

  section("a known UUID unparses to the canonical spelling");
  char text[37];
  uuid_unparse(kKnown, text);
  expect(std::strcmp(text, kText) == 0, std::string("unparse gives '") + kText + "'");
  expect(std::strlen(text) == 36, "and it is 36 characters");

  uuid_unparse_upper(kKnown, text);
  expect(std::strcmp(text, "12345678-9ABC-DEF0-1234-56789ABCDEF0") == 0, "upper case too");

  section("and parses back to the same sixteen bytes");
  uuid_t parsed;
  uuid_clear(parsed);
  expect(uuid_parse(kText, parsed) == 0, "parse succeeds");
  expect(uuid_compare(parsed, kKnown) == 0, "and the bytes match");

  uuid_t upper;
  uuid_clear(upper);
  expect(uuid_parse("12345678-9ABC-DEF0-1234-56789ABCDEF0", upper) == 0, "upper case parses");
  expect(uuid_compare(upper, kKnown) == 0, "to the same bytes");

  uuid_t no_dashes;
  uuid_clear(no_dashes);
  expect(uuid_parse("123456789abcdef0123456789abcdef0", no_dashes) == 0, "dashes are optional");
  expect(uuid_compare(no_dashes, kKnown) == 0, "and change nothing");

  section("a malformed spelling is refused rather than half read");
  uuid_t scratch;
  uuid_clear(scratch);
  expect(uuid_parse("12345678-9abc-def0-1234-56789abcdef", scratch) == -1, "too short");
  expect(uuid_parse("12345678-9abc-def0-1234-56789abcdef0a", scratch) == -1, "too long");
  expect(uuid_parse("", scratch) == -1, "empty");
  expect(uuid_is_null(scratch) == 1, "and a refused parse wrote nothing");

  section("compare orders by the bytes, and a copy compares equal");
  unsigned char same[16];
  uuid_copy(same, kKnown);
  expect(uuid_compare(same, kKnown) == 0, "a copy is equal");
  expect(uuid_compare(kKnown, same) == 0, "in both directions");
  unsigned char lower[16];
  uuid_copy(lower, kKnown);
  lower[15] -= 1;
  expect(uuid_compare(lower, kKnown) < 0, "a smaller last byte sorts first");
  expect(uuid_compare(kKnown, lower) > 0, "and the larger one after it");

  section("null is all zeroes, and clear makes one");
  uuid_t zeroed;
  uuid_generate(zeroed);
  expect(uuid_is_null(zeroed) == 0, "a generated UUID is not null");
  uuid_clear(zeroed);
  expect(uuid_is_null(zeroed) == 1, "a cleared one is");

  section("generate makes a version 4, variant 1 UUID, twice different");
  uuid_t first;
  uuid_t second;
  uuid_generate(first);
  uuid_generate(second);
  expect(uuid_is_null(first) == 0, "the first is not null");
  expect((first[6] & 0xf0) == 0x40, "its version nibble is 4");
  expect((first[8] & 0xc0) == 0x80, "its variant bits are 10");
  expect(uuid_compare(first, second) != 0, "and two calls do not repeat");

  uuid_unparse(first, text);
  uuid_t round;
  uuid_clear(round);
  expect(uuid_parse(text, round) == 0, "a generated UUID unparses and parses back");
  expect(uuid_compare(round, first) == 0, "to itself");

  if (failures != 0) {
    std::printf("\n%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("\nall checks passed\n");
  return 0;
}
