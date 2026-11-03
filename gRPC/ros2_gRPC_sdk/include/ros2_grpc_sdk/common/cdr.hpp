#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

#include "ros2_grpc_sdk/common/types.hpp"
#include "ros2_grpc_sdk/export.hpp"

namespace ros2_grpc_sdk::cdr {

// Minimal CDR (Common Data Representation) reader/writer covering the subset
// of primitives needed to (de)serialize the builtin ROS2 message types used
// by the SDK. The encoding is compatible with `rclcpp::SerializedMessage`:
// a 4 byte encapsulation header (0x00 0x01 0x00 0x00 for little endian plain
// CDR) followed by the payload. Alignment is computed relative to the start
// of the payload, i.e. after the encapsulation header.
class ROS2_GRPC_SDK_API Writer {
 public:
  explicit Writer(bool little_endian = true);

  void writeBool(bool value);
  void writeU8(std::uint8_t value);
  void writeU16(std::uint16_t value);
  void writeU32(std::uint32_t value);
  void writeU64(std::uint64_t value);
  void writeI8(std::int8_t value);
  void writeI16(std::int16_t value);
  void writeI32(std::int32_t value);
  void writeI64(std::int64_t value);
  void writeF32(float value);
  void writeF64(double value);
  void writeString(const std::string& value);
  void writeBytes(const Bytes& value);

  // Bulk append of `size` raw bytes, no alignment. This is the fast path for
  // primitive sequences: a caller that has already aligned the stream and
  // holds host-order little endian data (laser scans, point fields, images)
  // writes the whole array with one insert instead of one call per element.
  // Only meaningful when `little_endian()` is true - a big endian writer has
  // to keep going through the per-element calls, which byte swap.
  void writeRaw(const std::uint8_t* data, std::size_t size);

  // Padding for the next primitive of `alignment` bytes. Public so the
  // sequence fast paths above can align once and then stream the array.
  void align(std::size_t alignment);

  const Bytes& data() const { return buffer_; }
  Bytes take() { return std::move(buffer_); }
  bool little_endian() const { return little_endian_; }

 private:
  Bytes buffer_;
  bool little_endian_;
};

// Throws std::runtime_error when the buffer is exhausted or a string length
// is implausible. Use `ok()` to query the state after a sequence of reads.
class ROS2_GRPC_SDK_API Reader {
 public:
  explicit Reader(const Bytes& data);
  Reader(const std::uint8_t* data, std::size_t size);

  bool ok() const { return ok_; }
  std::size_t position() const { return pos_; }
  // Total size of the buffer that was handed in, header included.
  std::size_t size() const { return size_; }
  // Mark the stream as failed. A decoder that has already seen enough (an
  // impossible sequence length, for example) uses this to report a decode
  // failure instead of returning a value that looks valid.
  void fail() { ok_ = false; }

  bool readBool();
  std::uint8_t readU8();
  std::uint16_t readU16();
  std::uint32_t readU32();
  std::uint64_t readU64();
  std::int8_t readI8();
  std::int16_t readI16();
  std::int32_t readI32();
  std::int64_t readI64();
  float readF32();
  double readF64();
  std::string readString();
  Bytes readBytes(std::size_t length);

 private:
  void align(std::size_t alignment);
  void require(std::size_t n);
  template <typename T>
  T readPod();

  const std::uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t pos_ = 4;  // skip encapsulation header
  bool little_endian_ = true;
  bool ok_ = true;
};

}  // namespace ros2_grpc_sdk::cdr
