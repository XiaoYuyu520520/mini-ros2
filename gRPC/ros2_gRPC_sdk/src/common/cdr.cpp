#include "ros2_grpc_sdk/common/cdr.hpp"

#include <algorithm>
#include <iterator>

namespace ros2_grpc_sdk::cdr {
namespace {

constexpr std::size_t kHeaderSize = 4;

template <typename T>
void appendLittleEndian(Bytes& buffer, T value) {
  const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
  buffer.insert(buffer.end(), raw, raw + sizeof(T));
}

template <typename T>
void appendBigEndian(Bytes& buffer, T value) {
  const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
  buffer.insert(buffer.end(), std::make_reverse_iterator(raw + sizeof(T)),
                std::make_reverse_iterator(raw));
}

template <typename T>
void appendScalar(Bytes& buffer, T value, bool little_endian) {
  if (little_endian) {
    appendLittleEndian(buffer, value);
  } else {
    appendBigEndian(buffer, value);
  }
}

std::size_t paddingFor(std::size_t position, std::size_t alignment) {
  const std::size_t remainder = position % alignment;
  return remainder == 0 ? 0 : alignment - remainder;
}

}  // namespace

Writer::Writer(bool little_endian) : little_endian_(little_endian) {
  buffer_.reserve(64);
  buffer_.push_back(0x00);
  buffer_.push_back(little_endian ? 0x01 : 0x00);
  buffer_.push_back(0x00);
  buffer_.push_back(0x00);
}

void Writer::align(std::size_t alignment) {
  const std::size_t payload = buffer_.size() - kHeaderSize;
  const std::size_t pad = paddingFor(payload, alignment);
  buffer_.insert(buffer_.end(), pad, 0x00);
}

void Writer::writeBool(bool value) { writeU8(value ? 1 : 0); }

void Writer::writeU8(std::uint8_t value) {
  buffer_.push_back(value);
}

void Writer::writeI8(std::int8_t value) {
  buffer_.push_back(static_cast<std::uint8_t>(value));
}

void Writer::writeU16(std::uint16_t value) {
  align(2);
  appendScalar(buffer_, value, little_endian_);
}

void Writer::writeU32(std::uint32_t value) {
  align(4);
  appendScalar(buffer_, value, little_endian_);
}

void Writer::writeU64(std::uint64_t value) {
  align(8);
  appendScalar(buffer_, value, little_endian_);
}

void Writer::writeI16(std::int16_t value) {
  writeU16(static_cast<std::uint16_t>(value));
}

void Writer::writeI32(std::int32_t value) {
  writeU32(static_cast<std::uint32_t>(value));
}

void Writer::writeI64(std::int64_t value) {
  writeU64(static_cast<std::uint64_t>(value));
}

void Writer::writeF32(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  writeU32(bits);
}

void Writer::writeF64(double value) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  writeU64(bits);
}

void Writer::writeString(const std::string& value) {
  // ROS2 follows the DDS-CDR convention: the 32 bit length includes the
  // trailing null terminator.
  writeU32(static_cast<std::uint32_t>(value.size() + 1));
  buffer_.insert(buffer_.end(), value.begin(), value.end());
  buffer_.push_back(0x00);
}

void Writer::writeBytes(const Bytes& value) {
  buffer_.insert(buffer_.end(), value.begin(), value.end());
}

Reader::Reader(const Bytes& data) : Reader(data.data(), data.size()) {}

Reader::Reader(const std::uint8_t* data, std::size_t size)
    : data_(data), size_(size), pos_(kHeaderSize) {
  if (data_ == nullptr || size_ < kHeaderSize) {
    ok_ = false;
    pos_ = size_;
    return;
  }
  little_endian_ = (data_[1] & 0x01) != 0;
}

void Reader::align(std::size_t alignment) {
  const std::size_t payload = pos_ - kHeaderSize;
  pos_ += paddingFor(payload, alignment);
}

void Reader::require(std::size_t n) {
  if (!ok_) {
    return;
  }
  if (pos_ + n > size_) {
    ok_ = false;
  }
}

template <typename T>
T Reader::readPod() {
  T value{};
  align(sizeof(T));
  require(sizeof(T));
  if (!ok_) {
    return value;
  }
  const std::uint8_t* src = data_ + pos_;
  if (little_endian_) {
    std::memcpy(&value, src, sizeof(T));
  } else {
    auto* dst = reinterpret_cast<std::uint8_t*>(&value);
    std::reverse_copy(src, src + sizeof(T), dst);
  }
  pos_ += sizeof(T);
  return value;
}

bool Reader::readBool() { return readU8() != 0; }
std::uint8_t Reader::readU8() { return readPod<std::uint8_t>(); }
std::int8_t Reader::readI8() { return readPod<std::int8_t>(); }
std::uint16_t Reader::readU16() { return readPod<std::uint16_t>(); }
std::uint32_t Reader::readU32() { return readPod<std::uint32_t>(); }
std::uint64_t Reader::readU64() { return readPod<std::uint64_t>(); }
std::int16_t Reader::readI16() { return readPod<std::int16_t>(); }
std::int32_t Reader::readI32() { return readPod<std::int32_t>(); }
std::int64_t Reader::readI64() { return readPod<std::int64_t>(); }

float Reader::readF32() {
  const std::uint32_t bits = readU32();
  float value = 0.0F;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

double Reader::readF64() {
  const std::uint64_t bits = readU64();
  double value = 0.0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::string Reader::readString() {
  const std::uint32_t length = readU32();
  if (!ok_) {
    return {};
  }
  if (length == 0) {
    return {};
  }
  // Guard against corrupt/hostile lengths: 64 MiB is far above any legitimate
  // string carried by this SDK.
  constexpr std::uint32_t kMaxStringLength = 64U * 1024U * 1024U;
  if (length > kMaxStringLength) {
    ok_ = false;
    return {};
  }
  require(length);
  if (!ok_) {
    return {};
  }
  std::string value(reinterpret_cast<const char*>(data_ + pos_), length);
  if (!value.empty() && value.back() == '\0') {
    value.pop_back();
  }
  pos_ += length;
  return value;
}

Bytes Reader::readBytes(std::size_t length) {
  require(length);
  if (!ok_) {
    return {};
  }
  Bytes out(data_ + pos_, data_ + pos_ + length);
  pos_ += length;
  return out;
}

}  // namespace ros2_grpc_sdk::cdr
