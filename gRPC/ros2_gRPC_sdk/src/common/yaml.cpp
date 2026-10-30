#include "ros2_grpc_sdk/common/yaml.hpp"

#include <cctype>
#include <cstdlib>
#include <utility>

namespace ros2_grpc_sdk::yaml {
namespace {

const Value& nullValue() {
  static const Value value;
  return value;
}

std::string trimLeft(const std::string& text) {
  std::size_t i = 0;
  while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
    ++i;
  }
  return text.substr(i);
}

std::string trimRight(const std::string& text) {
  std::size_t end = text.size();
  while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                     text[end - 1] == '\r')) {
    --end;
  }
  return text.substr(0, end);
}

std::string trim(const std::string& text) { return trimLeft(trimRight(text)); }

std::string toLower(const std::string& text) {
  std::string out = text;
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

std::string unescapeDouble(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\\' && i + 1 < text.size()) {
      const char next = text[++i];
      switch (next) {
        case 'n':
          out.push_back('\n');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case '0':
          out.push_back('\0');
          break;
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        default:
          out.push_back(next);
          break;
      }
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

struct Line {
  int indent = 0;
  std::string content;
};

// Removes a trailing `# comment` unless it is inside quotes (a `#` only starts
// a comment at the beginning of a line or after whitespace).
std::string stripComment(const std::string& line) {
  bool in_single = false;
  bool in_double = false;
  std::string out;
  out.reserve(line.size());
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"' && !in_single) {
      in_double = !in_double;
      out.push_back(c);
    } else if (c == '\'' && !in_double) {
      in_single = !in_single;
      out.push_back(c);
    } else if (c == '#' && !in_single && !in_double &&
               (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) {
      break;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

class Parser {
 public:
  explicit Parser(std::vector<Line> lines) : lines_(std::move(lines)) {}

  bool parse(Value& out) {
    if (lines_.empty()) {
      out = Value();
      return true;
    }
    std::size_t index = 0;
    out = parseBlock(index, lines_[0].indent);
    if (!ok_) {
      return false;
    }
    if (index != lines_.size()) {
      return fail("unexpected content");
    }
    return true;
  }

  const std::string& error() const { return error_; }

 private:
  static bool isSequenceLine(const Line& line) {
    return line.content == "-" ||
           line.content.compare(0, 2, "- ") == 0;
  }

  bool fail(const std::string& message) {
    if (error_.empty()) {
      error_ = message;
    }
    ok_ = false;
    return false;
  }

  Value failValue(const std::string& message) {
    fail(message);
    return Value();
  }

  static std::size_t findColon(const std::string& text) {
    bool in_single = false;
    bool in_double = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
      const char c = text[i];
      if (c == '"' && !in_single) {
        in_double = !in_double;
      } else if (c == '\'' && !in_double) {
        in_single = !in_single;
      } else if (c == ':' && !in_single && !in_double) {
        return i;
      }
    }
    return std::string::npos;
  }

  Value parseBlock(std::size_t& index, int indent) {
    if (index >= lines_.size() || lines_[index].indent < indent) {
      return Value();
    }
    if (lines_[index].indent > indent) {
      return failValue("unexpected indentation");
    }
    if (isSequenceLine(lines_[index])) {
      return parseSequence(index, indent);
    }
    return parseMapping(index, indent);
  }

  Value parseSequence(std::size_t& index, int indent) {
    Value array = Value::array();
    while (index < lines_.size() && lines_[index].indent == indent &&
           isSequenceLine(lines_[index])) {
      const std::string rest = lines_[index].content.substr(1);
      const std::string item = trimLeft(rest);
      ++index;
      if (item.empty()) {
        if (index < lines_.size() && lines_[index].indent > indent) {
          array.push(parseBlock(index, lines_[index].indent));
        } else {
          array.push(Value());
        }
      } else {
        array.push(parseScalarOrFlow(item));
      }
      if (!ok_) {
        break;
      }
    }
    return array;
  }

  Value parseMapping(std::size_t& index, int indent) {
    Value object = Value::object();
    while (index < lines_.size() && lines_[index].indent == indent &&
           !isSequenceLine(lines_[index])) {
      const std::string& content = lines_[index].content;
      const std::size_t colon = findColon(content);
      if (colon == std::string::npos) {
        return failValue("expected 'key: value'");
      }
      const std::string key = trim(content.substr(0, colon));
      const std::string rest = trimLeft(content.substr(colon + 1));
      ++index;
      if (key.empty()) {
        return failValue("empty mapping key");
      }
      if (rest.empty()) {
        if (index < lines_.size() && lines_[index].indent > indent) {
          object[key] = parseBlock(index, lines_[index].indent);
        } else if (index < lines_.size() &&
                   lines_[index].indent == indent &&
                   isSequenceLine(lines_[index])) {
          // Sequence written at the same indentation as its key, e.g.
          //   ports:
          //   - 50051
          object[key] = parseSequence(index, indent);
        } else {
          object[key] = Value();
        }
      } else {
        object[key] = parseScalarOrFlow(rest);
      }
      if (!ok_) {
        break;
      }
    }
    return object;
  }

  Value parseScalarOrFlow(const std::string& text) {
    const std::string value = trim(text);
    if (!value.empty() && value.front() == '[') {
      return parseFlowSequence(value);
    }
    return parseScalar(value);
  }

  Value parseFlowSequence(const std::string& text) {
    Value array = Value::array();
    if (text.size() < 2 || text.front() != '[' || text.back() != ']') {
      return failValue("invalid flow sequence");
    }
    const std::string inner = text.substr(1, text.size() - 2);
    std::vector<std::string> parts;
    std::string current;
    bool in_single = false;
    bool in_double = false;
    for (char c : inner) {
      if (c == '"' && !in_single) {
        in_double = !in_double;
        current.push_back(c);
      } else if (c == '\'' && !in_double) {
        in_single = !in_single;
        current.push_back(c);
      } else if (c == ',' && !in_single && !in_double) {
        parts.push_back(current);
        current.clear();
      } else {
        current.push_back(c);
      }
    }
    if (!trim(current).empty() || !parts.empty()) {
      parts.push_back(current);
    }
    for (const auto& part : parts) {
      const std::string item = trim(part);
      if (!item.empty()) {
        array.push(parseScalar(item));
      }
    }
    return array;
  }

  Value parseScalar(const std::string& text) {
    const std::string value = trim(text);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      return Value(unescapeDouble(value.substr(1, value.size() - 2)));
    }
    if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
      const std::string inner = value.substr(1, value.size() - 2);
      std::string out;
      for (std::size_t i = 0; i < inner.size(); ++i) {
        if (inner[i] == '\'' && i + 1 < inner.size() && inner[i + 1] == '\'') {
          out.push_back('\'');
          ++i;
        } else {
          out.push_back(inner[i]);
        }
      }
      return Value(out);
    }

    const std::string lower = toLower(value);
    if (value.empty() || lower == "null" || value == "~") {
      return Value();
    }
    if (lower == "true" || lower == "yes" || lower == "on") {
      return Value(true);
    }
    if (lower == "false" || lower == "no" || lower == "off") {
      return Value(false);
    }

    char* end = nullptr;
    const double number = std::strtod(value.c_str(), &end);
    if (end != value.c_str() && end != nullptr && *end == '\0') {
      return Value(number);
    }
    return Value(value);
  }

  std::vector<Line> lines_;
  bool ok_ = true;
  std::string error_;
};

}  // namespace

Value::Value(bool boolean) : type_(Type::Bool), boolean_(boolean) {}
Value::Value(double number) : type_(Type::Number), number_(number) {}
Value::Value(int number)
    : type_(Type::Number), number_(static_cast<double>(number)) {}
Value::Value(std::string text) : type_(Type::String), text_(std::move(text)) {}
Value::Value(const char* text)
    : type_(Type::String), text_(text != nullptr ? text : "") {}

Value Value::array() {
  Value value;
  value.type_ = Type::Array;
  return value;
}

Value Value::object() {
  Value value;
  value.type_ = Type::Object;
  return value;
}

bool Value::asBool(bool fallback) const {
  return type_ == Type::Bool ? boolean_ : fallback;
}

double Value::asNumber(double fallback) const {
  return type_ == Type::Number ? number_ : fallback;
}

int Value::asInt(int fallback) const {
  return type_ == Type::Number ? static_cast<int>(number_) : fallback;
}

std::string Value::asString(const std::string& fallback) const {
  return type_ == Type::String ? text_ : fallback;
}

std::size_t Value::size() const {
  if (type_ == Type::Array) {
    return items_.size();
  }
  if (type_ == Type::Object) {
    return values_.size();
  }
  return 0;
}

const Value& Value::at(std::size_t index) const {
  if (type_ == Type::Array && index < items_.size()) {
    return items_[index];
  }
  if (type_ == Type::Object && index < values_.size()) {
    return values_[index];
  }
  return nullValue();
}

Value& Value::at(std::size_t index) {
  if (type_ == Type::Array) {
    return items_.at(index);
  }
  if (type_ == Type::Object) {
    return values_.at(index);
  }
  static Value dummy;
  return dummy;
}

void Value::push(Value value) {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    items_.clear();
  }
  items_.push_back(std::move(value));
}

bool Value::has(const std::string& key) const { return find(key) != nullptr; }

const Value* Value::find(const std::string& key) const {
  if (type_ != Type::Object) {
    return nullptr;
  }
  for (std::size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) {
      return &values_[i];
    }
  }
  return nullptr;
}

Value& Value::operator[](const std::string& key) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    keys_.clear();
    values_.clear();
  }
  for (std::size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) {
      return values_[i];
    }
  }
  keys_.push_back(key);
  values_.push_back(Value());
  return values_.back();
}

const Value& Value::operator[](const std::string& key) const {
  const Value* value = find(key);
  return value != nullptr ? *value : nullValue();
}

bool Value::parse(const std::string& text, Value& out, std::string* error) {
  std::vector<Line> lines;
  std::size_t position = 0;
  int line_number = 0;
  while (position <= text.size()) {
    const std::size_t next = text.find('\n', position);
    const std::string raw =
        next == std::string::npos ? text.substr(position)
                                  : text.substr(position, next - position);
    ++line_number;

    const std::string without_comment = trimRight(stripComment(raw));

    std::size_t first = 0;
    while (first < without_comment.size() && without_comment[first] == ' ') {
      ++first;
    }
    if (first < without_comment.size() &&
        without_comment[first] == '\t') {
      if (error != nullptr) {
        *error = "tabs are not allowed for indentation (line " +
                 std::to_string(line_number) + ")";
      }
      return false;
    }
    const std::string content = without_comment.substr(first);
    if (content.empty() || content == "---" || content == "...") {
      // Blank line, comment or document marker.
    } else {
      Line line;
      line.indent = static_cast<int>(first);
      line.content = content;
      lines.push_back(std::move(line));
    }

    if (next == std::string::npos) {
      break;
    }
    position = next + 1;
  }

  Parser parser(std::move(lines));
  if (!parser.parse(out)) {
    if (error != nullptr) {
      *error = parser.error();
    }
    return false;
  }
  return true;
}

}  // namespace ros2_grpc_sdk::yaml
