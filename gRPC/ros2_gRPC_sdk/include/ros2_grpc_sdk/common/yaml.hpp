#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ros2_grpc_sdk/export.hpp"

namespace ros2_grpc_sdk::yaml {

// Minimal, dependency-free YAML value used to read the SDK config file.
//
// The parser supports the YAML subset needed for configuration and, crucially,
// **comments** (`# ...`), which JSON does not:
//
//   server:
//     address: "0.0.0.0:50051"   # reachable on the whole LAN
//   discovery:
//     ports: [50051, 50052]
//     include_self: false
//
// Supported: block mappings/sequences by indentation, inline flow sequences
// `[a, b]`, quoted and plain scalars, booleans (true/false/yes/no/on/off),
// null (`null`, `~`) and numbers.
class ROS2_GRPC_SDK_API Value {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Value() = default;
  explicit Value(bool boolean);
  explicit Value(double number);
  explicit Value(int number);
  explicit Value(std::string text);
  explicit Value(const char* text);

  static Value array();
  static Value object();

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isBool() const { return type_ == Type::Bool; }
  bool isNumber() const { return type_ == Type::Number; }
  bool isString() const { return type_ == Type::String; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }

  bool asBool(bool fallback = false) const;
  double asNumber(double fallback = 0.0) const;
  int asInt(int fallback = 0) const;
  std::string asString(const std::string& fallback = std::string()) const;

  // Arrays.
  std::size_t size() const;
  const Value& at(std::size_t index) const;
  Value& at(std::size_t index);
  void push(Value value);

  // Objects.
  bool has(const std::string& key) const;
  const Value* find(const std::string& key) const;
  Value& operator[](const std::string& key);
  const Value& operator[](const std::string& key) const;

  // Parses a YAML document. Returns false and fills `error` on failure.
  static bool parse(const std::string& text, Value& out,
                    std::string* error = nullptr);

 private:
  Type type_ = Type::Null;
  bool boolean_ = false;
  double number_ = 0.0;
  std::string text_;
  std::vector<Value> items_;       // Array elements.
  std::vector<std::string> keys_;  // Object keys.
  std::vector<Value> values_;      // Object values (parallel to keys).
};

}  // namespace ros2_grpc_sdk::yaml
