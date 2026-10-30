#ifndef ROS2_SHIM_SERIALIZATION_HPP
#define ROS2_SHIM_SERIALIZATION_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ros2_shim
{

// Binary format (little endian, no alignment padding):
//   integral / floating point : raw bytes
//   bool                      : 1 byte (0/1)
//   string                    : uint32 length + bytes
//   u16string                 : uint32 length + uint16 units
//   sequence<T>               : uint32 count + elements
//   array<T,N>                : N elements
//   message                   : fields in declaration order
namespace detail
{
template <class T, class = void>
struct is_message : std::false_type {};

template <class T>
struct is_message<T, std::void_t<typename T::ros2_message_tag>> : std::true_type {};

template <class T>
struct is_vector : std::false_type {};

template <class T, class A>
struct is_vector<std::vector<T, A>> : std::true_type {};

template <class T>
struct is_array : std::false_type {};

template <class T, std::size_t N>
struct is_array<std::array<T, N>> : std::true_type {};

template <class T>
inline constexpr bool is_write_scalar =
  (std::is_integral_v<T> || std::is_floating_point_v<T>) && !std::is_same_v<T, bool>;
}  // namespace detail

class WriteArchive
{
public:
  explicit WriteArchive(std::vector<uint8_t> & out)
  : out_(out)
  {
  }

  template <class T>
  void operator()(const T & value)
  {
    write(value);
  }

  std::vector<uint8_t> & buffer() {return out_;}

private:
  template <class T>
  void write(const T & value)
  {
    using U = std::decay_t<T>;
    if constexpr (detail::is_message<U>::value) {
      value.visit(*this);
    } else if constexpr (std::is_same_v<U, std::string>) {
      write_u32(static_cast<uint32_t>(value.size()));
      out_.insert(out_.end(), value.begin(), value.end());
    } else if constexpr (std::is_same_v<U, std::u16string>) {
      write_u32(static_cast<uint32_t>(value.size()));
      for (char16_t c : value) {
        write_le(static_cast<uint16_t>(c));
      }
    } else if constexpr (detail::is_vector<U>::value) {
      write_u32(static_cast<uint32_t>(value.size()));
      for (const auto & element : value) {
        write(element);
      }
    } else if constexpr (detail::is_array<U>::value) {
      for (const auto & element : value) {
        write(element);
      }
    } else if constexpr (std::is_same_v<U, bool>) {
      out_.push_back(value ? uint8_t{1} : uint8_t{0});
    } else if constexpr (detail::is_write_scalar<U>) {
      write_le(value);
    } else {
      static_assert(!sizeof(U), "unsupported ROS field type for serialization");
    }
  }

  void write_u32(uint32_t value) {write_le(value);}

  template <class T>
  void write_le(const T & value)
  {
    uint8_t bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));
    out_.insert(out_.end(), bytes, bytes + sizeof(T));
  }

  std::vector<uint8_t> & out_;
};

class ReadArchive
{
public:
  ReadArchive(const uint8_t * data, std::size_t size)
  : data_(data), size_(size)
  {
  }

  template <class T>
  void operator()(T & value)
  {
    read(value);
  }

  bool ok() const {return ok_;}
  std::size_t consumed() const {return pos_;}
  std::size_t remaining() const {return size_ - pos_;}

private:
  template <class T>
  void read(T & value)
  {
    using U = std::decay_t<T>;
    if constexpr (detail::is_message<U>::value) {
      value.visit(*this);
    } else if constexpr (std::is_same_v<U, std::string>) {
      uint32_t n = 0;
      read_u32(n);
      if (!ok_ || remaining() < n) {
        ok_ = false;
        return;
      }
      value.assign(reinterpret_cast<const char *>(data_ + pos_), n);
      pos_ += n;
    } else if constexpr (std::is_same_v<U, std::u16string>) {
      uint32_t n = 0;
      read_u32(n);
      value.clear();
      value.reserve(n <= remaining() / 2 ? n : 0);
      for (uint32_t i = 0; i < n && ok_; ++i) {
        uint16_t c = 0;
        read_le(c);
        value.push_back(static_cast<char16_t>(c));
      }
    } else if constexpr (detail::is_vector<U>::value) {
      uint32_t n = 0;
      read_u32(n);
      if (!ok_ || n > remaining()) {
        ok_ = false;
        return;
      }
      value.clear();
      value.reserve(n);
      for (uint32_t i = 0; i < n && ok_; ++i) {
        typename U::value_type element{};
        read(element);
        value.push_back(std::move(element));
      }
    } else if constexpr (detail::is_array<U>::value) {
      for (auto & element : value) {
        read(element);
        if (!ok_) {
          break;
        }
      }
    } else if constexpr (std::is_same_v<U, bool>) {
      uint8_t b = 0;
      read_raw(&b, 1);
      value = (b != 0);
    } else if constexpr (detail::is_write_scalar<U>) {
      read_le(value);
    } else {
      static_assert(!sizeof(U), "unsupported ROS field type for serialization");
    }
  }

  void read_u32(uint32_t & value) {read_le(value);}

  template <class T>
  void read_le(T & value)
  {
    read_raw(&value, sizeof(T));
  }

  void read_raw(void * dst, std::size_t n)
  {
    if (!ok_ || remaining() < n) {
      ok_ = false;
      return;
    }
    std::memcpy(dst, data_ + pos_, n);
    pos_ += n;
  }

  const uint8_t * data_;
  std::size_t size_;
  std::size_t pos_ = 0;
  bool ok_ = true;
};

template <class Message>
std::vector<uint8_t> serialize(const Message & message)
{
  std::vector<uint8_t> out;
  WriteArchive archive(out);
  message.visit(archive);
  return out;
}

template <class Message>
bool deserialize(const uint8_t * data, std::size_t size, Message & message)
{
  ReadArchive archive(data, size);
  message.visit(archive);
  return archive.ok();
}

template <class Message>
bool deserialize(const std::vector<uint8_t> & data, Message & message)
{
  return deserialize(data.data(), data.size(), message);
}

}  // namespace ros2_shim

#endif  // ROS2_SHIM_SERIALIZATION_HPP
