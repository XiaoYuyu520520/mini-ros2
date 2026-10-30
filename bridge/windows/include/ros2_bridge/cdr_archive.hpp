#ifndef ROS2_BRIDGE_CDR_ARCHIVE_HPP
#define ROS2_BRIDGE_CDR_ARCHIVE_HPP

// Converts between the two message layouts used in this project:
//
//   * the shared-memory shim's native layout (nav2/ros2_shim/include/ros2_shim/
//     serialization.hpp): plain little endian fields, 4 byte string/sequence
//     lengths, no padding at all;
//   * real ROS2 DDS-CDR, i.e. what rclcpp/rclpy put on the wire.
//
// Both archives plug into the `visit()` method that
// nav2/ros2_shim/tools/generate_messages.py emits for every ROS2 message, so a
// single template pair transcodes every generated type in either direction.
//
// The CDR primitives come from the gRPC SDK (`ros2_grpc_sdk::cdr`), whose
// alignment rules were checked against eProsima Fast-CDR and `rosbags`:
// alignment is computed relative to the start of the CDR body, i.e. the first
// byte after the 4 byte encapsulation header.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "ros2_grpc_sdk/common/cdr.hpp"

namespace ros2_bridge
{

namespace detail
{

template<class T, class = void>
struct is_message : std::false_type {};

template<class T>
struct is_message<T, std::void_t<typename T::ros2_message_tag>> : std::true_type {};

template<class T>
struct is_vector : std::false_type {};

template<class T, class Allocator>
struct is_vector<std::vector<T, Allocator>> : std::true_type {};

template<class T>
struct is_array : std::false_type {};

template<class T, std::size_t N>
struct is_array<std::array<T, N>> : std::true_type {};

// `std::vector<bool>` is the one byte-sized integral vector that is *not* a
// byte buffer: its elements are bit proxies, `data()` is deleted, and its CDR
// representation is one byte per element like every other sequence. It has to
// fall through to the element-by-element path, which is why `bool` is excluded
// here (`nav2_msgs/msg/CriticsStats` carries a `bool[]` and would otherwise not
// compile at all).
template<class T>
inline constexpr bool is_byte_vector =
  is_vector<T>::value && sizeof(typename T::value_type) == 1 &&
  std::is_integral_v<typename T::value_type> &&
  !std::is_same_v<typename T::value_type, bool>;

template<class T>
inline constexpr bool is_scalar =
  (std::is_integral_v<T> || std::is_floating_point_v<T>) && !std::is_same_v<T, bool>;

}  // namespace detail

// ------------------------------------------------------------ writing (shim -> CDR)

class CdrWriteArchive
{
public:
  explicit CdrWriteArchive(ros2_grpc_sdk::cdr::Writer & writer)
  : writer_(writer)
  {
  }

  template<class T>
  void operator()(const T & value)
  {
    write(value);
  }

private:
  void write(bool value) {writer_.writeBool(value);}
  void write(std::uint8_t value) {writer_.writeU8(value);}
  void write(std::int8_t value) {writer_.writeI8(value);}
  void write(std::uint16_t value) {writer_.writeU16(value);}
  void write(std::int16_t value) {writer_.writeI16(value);}
  void write(std::uint32_t value) {writer_.writeU32(value);}
  void write(std::int32_t value) {writer_.writeI32(value);}
  void write(std::uint64_t value) {writer_.writeU64(value);}
  void write(std::int64_t value) {writer_.writeI64(value);}
  void write(float value) {writer_.writeF32(value);}
  void write(double value) {writer_.writeF64(value);}

  // `char` is used by a few generated messages for `uint8` data.
  void write(char value) {writer_.writeU8(static_cast<std::uint8_t>(value));}

  void write(const std::string & value) {writer_.writeString(value);}

  void write(const std::u16string & value)
  {
    writer_.writeU32(static_cast<std::uint32_t>(value.size() + 1));
    for (char16_t unit : value) {
      writer_.writeU16(static_cast<std::uint16_t>(unit));
    }
    writer_.writeU16(0);
  }

  template<class T>
  void write(const std::vector<T> & value)
  {
    writer_.writeU32(static_cast<std::uint32_t>(value.size()));
    if constexpr (detail::is_scalar<T>) {
      for (const T & element : value) {
        write(element);
      }
    } else {
      for (const T & element : value) {
        write(element);
      }
    }
  }

  template<class T, std::size_t N>
  void write(const std::array<T, N> & value)
  {
    for (const T & element : value) {
      write(element);
    }
  }

  template<class T>
  void write(const T & value)
  {
    static_assert(
      detail::is_message<T>::value,
      "CdrWriteArchive: unsupported field type in generated ROS2 message");
    value.visit(*this);
  }

  ros2_grpc_sdk::cdr::Writer & writer_;
};

// ------------------------------------------------------------ reading (CDR -> shim)

class CdrReadArchive
{
public:
  // `data` must be a complete CDR payload including the encapsulation header.
  CdrReadArchive(const std::uint8_t * data, std::size_t size)
  : reader_(data, size)
  {
  }

  explicit CdrReadArchive(const std::vector<std::uint8_t> & data)
  : reader_(data.data(), data.size())
  {
  }

  template<class T>
  void operator()(T & value)
  {
    read(value);
  }

  bool ok() const {return reader_.ok();}

private:
  void read(bool & value) {value = reader_.readBool();}
  void read(std::uint8_t & value) {value = reader_.readU8();}
  void read(std::int8_t & value) {value = reader_.readI8();}
  void read(std::uint16_t & value) {value = reader_.readU16();}
  void read(std::int16_t & value) {value = reader_.readI16();}
  void read(std::uint32_t & value) {value = reader_.readU32();}
  void read(std::int32_t & value) {value = reader_.readI32();}
  void read(std::uint64_t & value) {value = reader_.readU64();}
  void read(std::int64_t & value) {value = reader_.readI64();}
  void read(float & value) {value = reader_.readF32();}
  void read(double & value) {value = reader_.readF64();}
  void read(char & value) {value = static_cast<char>(reader_.readU8());}

  void read(std::string & value) {value = reader_.readString();}

  void read(std::u16string & value)
  {
    const std::uint32_t count = reader_.readU32();
    value.clear();
    if (count > (1u << 20)) {
      return;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
      const std::uint16_t unit = reader_.readU16();
      if (i + 1 < count) {
        value.push_back(static_cast<char16_t>(unit));
      }
    }
  }

  template<class T>
  void read(std::vector<T> & value)
  {
    const std::uint32_t count = reader_.readU32();
    value.clear();
    if (count > (1u << 26)) {
      return;
    }
    if constexpr (detail::is_byte_vector<std::vector<T>>) {
      value.resize(count);
      if (count > 0) {
        const auto bytes = reader_.readBytes(count);
        if (bytes.size() == count) {
          std::memcpy(value.data(), bytes.data(), count);
        }
      }
      return;
    }
    value.reserve(count);
    for (std::uint32_t i = 0; i < count && reader_.ok(); ++i) {
      T element{};
      read(element);
      value.push_back(std::move(element));
    }
  }

  template<class T, std::size_t N>
  void read(std::array<T, N> & value)
  {
    for (std::size_t i = 0; i < N && reader_.ok(); ++i) {
      read(value[i]);
    }
  }

  template<class T>
  void read(T & value)
  {
    static_assert(
      detail::is_message<T>::value,
      "CdrReadArchive: unsupported field type in generated ROS2 message");
    value.visit(*this);
  }

  ros2_grpc_sdk::cdr::Reader reader_;
};

}  // namespace ros2_bridge

#endif  // ROS2_BRIDGE_CDR_ARCHIVE_HPP