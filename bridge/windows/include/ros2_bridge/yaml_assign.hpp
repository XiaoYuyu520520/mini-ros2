#ifndef ROS2_BRIDGE_YAML_ASSIGN_HPP
#define ROS2_BRIDGE_YAML_ASSIGN_HPP

// The YAML half of `ros2 topic pub` / `ros2 service call` / `ros2 action
// send_goal`.
//
// Real ROS2 reads the last positional argument of those three subcommands as
// one YAML document - `ros2 topic pub --help` says so in as many words,
// "Values to fill the message with in YAML format (e.g. 'data: Hello World')"
// - and everything below is written the same way:
//
//   ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.5}}"
//   ros2 service call /set_bool std_srvs/srv/SetBool "{data: true}"
//   ros2 topic pub /scan sensor_msgs/msg/LaserScan "{ranges: [1.0, 2.0]}"
//
// This header flattens such a document into the `field.path=value` assignment
// list the generated-message codecs already understand (`applyFields`), so the
// nested and the flat spelling end up in exactly the same place:
//
//   {linear: {x: 0.5}}    ->  linear.x=0.5
//   {a: 1, b: 2}          ->  a=1  and  b=2
//   {ranges: [1.0, 2.0]}  ->  ranges=1.0, 2.0    (the codec's list syntax)
//
// The flat spelling keeps working, because the .bat demos, the round-trip
// tests and the running guides are written in it:
//
//   ros2 topic pub /cmd_vel geometry_msgs/msg/Twist linear.x=0.5 --once
//   ros2 topic pub /chatter std_msgs/msg/String data=hi --once
//
// Both of YAML's shapes are read, because both are written by hand:
//
//   {linear: {x: 0.5}}        flow, the one line spelling
//   linear:                   block, which is what a multi-line value looks
//     x: 0.5                  like when it comes from a params file
//
// Supported subset: `{...}` flow mappings (nested, and they may run over
// several lines), `[...]` flow sequences, block mappings with either an inline
// value (`linear: {x: 0.5}`) or a nested block under the key, single and double
// quoted scalars (one level of quotes is removed, with the `''` and `\n` /
// `\t` / `\\` escapes), bare scalars, the null forms (`null`, `Null`, `NULL`,
// `~`, empty) as an empty value, `#` comments to the end of the line, and the
// two flat shorthands `field={...}` / `field=[...]` (which nest under the field
// instead of replacing its value).
//
// In block style a bare scalar ends with the line, which is what YAML does and
// what keeps `data: hi` + a following `other: 1` from becoming one value that
// reads `hi\nother: 1`. In flow style a bare scalar ends at the `,` / `}` that
// closes it.
//
// A sequence is read in the two shapes the messages have:
//
//   {ranges: [1.0, 2.0]}                          scalars, which reach the
//                                                 codec as the list
//                                                 `ranges=1.0, 2.0`
//   {poses: {goals: [{pose: {position: {x: 1}}}]}} messages, one per `[...]`,
//                                                 which reach it as
//                                                 `poses.goals[0].pose.position.x=1`
//
// The second shape is what `nav_msgs/msg/Path`, `nav_msgs/msg/Goals` and the
// waypoint goals of nav2 are: a `geometry_msgs/PoseStamped[]` has no comma
// separated spelling at all. A sequence that mixes the two, or a sequence
// inside a sequence, is an error rather than a silently different message.
//
// Scalars are handed on as text, so `yes`, `1e-3` and `01` reach the field
// setters in field_parse.hpp exactly as written and are read there for the
// field's own type. That is why `{data: yes}` still means the three letters
// "yes" in a `std_msgs/msg/String`, while a bool field reads it as true.

#include <cctype>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "ros2_bridge/type_registry.hpp"

namespace ros2_bridge
{
namespace yaml_assign
{

// What one command line token is:
//
//   Neither   a topic name, a type name, anything else that is not a value
//   Flat      one `field.path=value` assignment
//   Document  the start of a YAML document (`{a: 1}`, `[1, 2]`, `a: 1`)
//
// The decision goes by whichever of `=` and `:` / `{` / `[` comes first:
// `linear={x: 1}` is the flat form with a nested value, `{a: 1, b: 2}` is a
// document.
enum class Form
{
  Neither,
  Flat,
  Document,
};

inline bool isValueSpace(char character)
{
  return character == ' ' || character == '\t' || character == '\n' ||
         character == '\r';
}

// A space that belongs to the line rather than ending it: the indentation and
// the trailing `\r` of a CRLF file. Block style leans on this to tell `x: 0.5`
// (a value on the line) from `x:` (the next block is indented deeper).
inline bool isLineSpace(char character)
{
  return character == ' ' || character == '\t' || character == '\r';
}

inline std::string trim(const std::string & text)
{
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && isValueSpace(text[begin])) {
    ++begin;
  }
  while (end > begin && isValueSpace(text[end - 1])) {
    --end;
  }
  return text.substr(begin, end - begin);
}

// Is the text in front of an `=` a field path (`linear.x`, `poses[2].pose`)
// rather than the start of a YAML document?
//
// The two spellings are told apart by where the `=` sits: `linear={x: 1}` is
// the flat form and `{linear: {x: 1}}` is a document. An indexed path breaks
// that reading, because the `[` of `poses[0]` comes before the `=` of
// `poses[0].x=1` and would make the token a document; the shape of a path is
// therefore checked first and wins over the bracket.
inline bool looksLikeFieldPath(const std::string & text)
{
  std::size_t pos = 0;
  while (pos < text.size()) {
    const std::size_t name = pos;
    while (pos < text.size() &&
      (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_'))
    {
      ++pos;
    }
    if (pos == name) {
      return false;  // a name is owed here
    }
    if (pos == text.size()) {
      return true;
    }
    if (text[pos] == '[') {
      const std::size_t close = text.find(']', pos);
      if (close == std::string::npos || close == pos + 1) {
        return false;
      }
      for (std::size_t i = pos + 1; i < close; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
          return false;
        }
      }
      pos = close + 1;
      if (pos == text.size()) {
        return true;
      }
      if (text[pos] != '.') {
        return false;
      }
    } else if (text[pos] != '.') {
      return false;
    }
    ++pos;
  }
  return false;
}

inline Form classify(const std::string & token)
{
  const std::string text = trim(token);
  if (text.empty()) {
    return Form::Neither;
  }
  const std::size_t equals = text.find('=');
  if (equals != std::string::npos && looksLikeFieldPath(text.substr(0, equals))) {
    return Form::Flat;
  }
  const std::size_t document = text.find_first_of(":{[");
  if (equals != std::string::npos &&
    (document == std::string::npos || equals < document))
  {
    return Form::Flat;
  }
  if (document != std::string::npos) {
    return Form::Document;
  }
  return Form::Neither;
}

// How many `{` / `[` are still open in `text` (quotes ignored). A shell that
// splits `{linear: {x: 0.5}}` into three arguments leaves the caller with a
// positive depth until the last piece is in, which is how the pieces are glued
// back together - see `cmdTopicPub` in tools/ros2_cli.cpp.
inline int openDepth(const std::string & text)
{
  int depth = 0;
  bool in_single = false;
  bool in_double = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '"' && !in_single) {
      in_double = !in_double;
    } else if (c == '\'' && !in_double) {
      in_single = !in_single;
    } else if (!in_single && !in_double) {
      if (c == '{' || c == '[') {
        ++depth;
      } else if (c == '}' || c == ']') {
        --depth;
      }
    }
  }
  return depth > 0 ? depth : 0;
}

// Reads one quoted scalar, `''` / `\n` style escapes included. Public because
// the flat `field=value` form uses it to drop the quotes a shell left in
// place: `data="hi"` has to reach the codec as `hi`.
inline bool decodeQuoted(const std::string & text, std::size_t & pos, std::string & out,
  std::string & error)
{
  if (pos >= text.size() || (text[pos] != '"' && text[pos] != '\'')) {
    error = "expected a quoted value";
    return false;
  }
  const char quote = text[pos++];
  std::string value;
  while (true) {
    if (pos >= text.size()) {
      error = "unterminated quoted value";
      return false;
    }
    const char c = text[pos++];
    if (c == quote) {
      if (quote == '\'' && pos < text.size() && text[pos] == '\'') {
        value.push_back('\'');
        ++pos;
        continue;
      }
      break;
    }
    if (quote == '"' && c == '\\' && pos < text.size()) {
      const char next = text[pos++];
      switch (next) {
        case 'n': value.push_back('\n'); break;
        case 't': value.push_back('\t'); break;
        case 'r': value.push_back('\r'); break;
        case '"': value.push_back('"'); break;
        case '\\': value.push_back('\\'); break;
        default: value.push_back(next); break;
      }
      continue;
    }
    value.push_back(c);
  }
  out = value;
  return true;
}

// The recursive descent parser behind `parseDocument`. It writes straight into
// the assignment list: a mapping nests into a longer dotted path, a sequence
// becomes the comma separated text `setSequence` expects, a scalar becomes the
// value of the path it was found under.
class Parser
{
 public:
  Parser(const std::string & text, std::string prefix, std::vector<FieldAssign> * out)
  : text_(text), prefix_(std::move(prefix)), out_(out)
  {
  }

  bool parse()
  {
    skipSpace();
    if (atEnd()) {
      return fail("empty YAML value");
    }
    if (peek() == '{') {
      if (!parseMapping(prefix_)) {
        return false;
      }
    } else if (peek() == '[') {
      return fail(
        "a message value is a mapping such as '{data: hi}', not a bare sequence; write the "
        "field name in front of it ('{ranges: [1.0, 2.0]}')");
    } else if (!parseBlockMapping(prefix_, -1)) {
      return false;
    }
    skipSpace();
    if (!atEnd()) {
      return fail("unexpected '" + trim(text_.substr(pos_)) + "' after the YAML value");
    }
    return true;
  }

  const std::string & error() const { return error_; }

  // The flat spelling `field=[1, 2]`: a top level sequence that belongs to one
  // field, so the elements come back as the codec's comma separated list.
  bool parseSequenceValue(const std::string & path)
  {
    skipSpace();
    if (atEnd() || peek() != '[') {
      return fail("expected a sequence");
    }
    if (!parseSequence(path)) {
      return false;
    }
    skipSpace();
    if (!atEnd()) {
      return fail("unexpected '" + trim(text_.substr(pos_)) + "' after the sequence");
    }
    return true;
  }

  void skipSpace()
  {
    while (!atEnd()) {
      const char c = text_[pos_];
      if (isValueSpace(c)) {
        ++pos_;
      } else if (c == '#' && (pos_ == 0 || isValueSpace(text_[pos_ - 1]))) {
        while (!atEnd() && text_[pos_] != '\n') {
          ++pos_;
        }
      } else {
        break;
      }
    }
  }

  bool atEnd() const { return pos_ >= text_.size(); }

 private:
  char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

  bool fail(const std::string & message)
  {
    if (error_.empty()) {
      error_ = message;
    }
    return false;
  }

  void emit(const std::string & path, const std::string & value)
  {
    if (out_ != nullptr) {
      out_->push_back(FieldAssign{path, value});
    }
  }

  static std::string join(const std::string & prefix, const std::string & key)
  {
    return prefix.empty() ? key : prefix + "." + key;
  }

  bool accept(char expected)
  {
    if (!atEnd() && peek() == expected) {
      ++pos_;
      return true;
    }
    return false;
  }

  // A `{...}` flow mapping. The value parser is told it is in flow style, so
  // there a comma always separates; in block style a comma only separates when
  // a `key:` follows it, which keeps `data: hi, there` in one piece while
  // `data: hi, x: 1` splits.
  bool parseMapping(const std::string & prefix)
  {
    ++pos_;  // '{'
    skipSpace();
    if (accept('}')) {
      return true;
    }
    while (true) {
      skipSpace();
      if (atEnd()) {
        return fail("unterminated '{'");
      }
      std::string key;
      if (!parseKey(key)) {
        return false;
      }
      const std::string path = join(prefix, key);
      skipSpace();
      if (!accept(':')) {
        return fail("expected ':' after the key '" + key + "'");
      }
      if (!parseValue(path, true)) {
        return false;
      }
      skipSpace();
      if (accept(',')) {
        skipSpace();
        if (accept('}')) {
          return true;  // trailing comma
        }
        if (atEnd()) {
          return fail("unterminated '{'");
        }
        continue;
      }
      if (accept('}')) {
        return true;
      }
      if (atEnd()) {
        return fail("unterminated '{'");
      }
      return fail(
        "expected ',' or '}' after the value of '" + path + "'");
    }
  }
  bool parseKey(std::string & key)
  {
    skipSpace();
    if (atEnd()) {
      return fail("expected a key");
    }
    if (peek() == '"' || peek() == '\'') {
      std::size_t pos = pos_;
      if (!decodeQuoted(text_, pos, key, error_)) {
        return false;
      }
      pos_ = pos;
      return true;
    }
    const std::size_t begin = pos_;
    while (!atEnd()) {
      const char c = text_[pos_];
      if (c == ':' || c == ',' || c == '{' || c == '}' || c == '[' || c == ']') {
        break;
      }
      ++pos_;
    }
    key = trim(text_.substr(begin, pos_ - begin));
    if (key.empty()) {
      return fail("expected a key before ':'");
    }
    return true;
  }

  bool parseValue(const std::string & path, bool flow)
  {
    skipSpace();
    if (atEnd()) {
      return fail("expected a value after the ':' of '" + path + "'");
    }
    const char c = peek();
    if (c == '{') {
      return parseMapping(path);
    }
    if (c == '[') {
      return parseSequence(path);
    }
    if (c == '"' || c == '\'') {
      std::string value;
      std::size_t pos = pos_;
      if (!decodeQuoted(text_, pos, value, error_)) {
        return false;
      }
      pos_ = pos;
      emit(path, value);
      return true;
    }
    const std::string scalar = parseScalar(flow);
    emit(path, isNullScalar(scalar) ? std::string() : scalar);
    return true;
  }

  // YAML's null forms and the empty scalar all mean "no value"; a field setter
  // reads that as the empty string or zero. Anything else is handed on as
  // text, so `yes` still reaches a string field as the three letters.
  static bool isNullScalar(const std::string & text)
  {
    return text.empty() || text == "~" || text == "null" || text == "Null" ||
           text == "NULL";
  }

  // A bare scalar runs to the next `,` / `}` / `]` at this level, to a ` #`
  // comment, or to the end of the text - and, in block style, to the end of the
  // line. That last one is the difference between `data: hi` followed by
  // `other: 1` being two fields and being one value spelled `hi\nother: 1`.
  std::string parseScalar(bool flow)
  {
    const std::size_t begin = pos_;
    std::size_t end = pos_;
    while (!atEnd()) {
      const char c = text_[pos_];
      if (c == '}' || c == ']') {
        break;
      }
      if (c == '\n' && !flow) {
        break;  // block style: the line ends the scalar
      }
      if (c == ',' && (flow || looksLikePair())) {
        break;
      }
      if (c == '#' && pos_ > begin && isValueSpace(text_[pos_ - 1])) {
        break;
      }
      ++pos_;
      end = pos_;
    }
    while (end > begin && isValueSpace(text_[end - 1])) {
      --end;
    }
    return text_.substr(begin, end - begin);
  }

  // After the `,` at pos_: does a `key:` follow before the next separator?
  bool looksLikePair() const
  {
    std::size_t i = pos_ + 1;
    while (i < text_.size() && isValueSpace(text_[i])) {
      ++i;
    }
    for (; i < text_.size(); ++i) {
      const char c = text_[i];
      if (c == ':') {
        return true;
      }
      if (c == ',' || c == '}' || c == ']') {
        return false;
      }
    }
    return false;
  }

  // A `[...]` value, in the two shapes the messages have. A sequence of scalars
  // is the codec's comma separated list (`setSequence`); a sequence of mappings
  // is one message per element, spelled under its index (`poses[0]....`,
  // `goals[2]....`), which is the only spelling a sequence of messages has.
  // Which one it is comes from the first element, and a sequence that mixes
  // them is refused rather than half read.
  bool parseSequence(const std::string & path)
  {
    ++pos_;  // '['
    std::size_t index = 0;
    int mode = 0;  // 0: nothing read yet, 1: scalars, 2: mappings
    std::string joined;
    while (true) {
      skipSpace();
      if (accept(']')) {
        break;
      }
      if (atEnd()) {
        return fail("unterminated '[' after the sequence of '" + path + "'");
      }
      if (accept(',')) {
        continue;  // empty element / trailing comma
      }
      if (peek() == '{') {
        if (mode == 1) {
          return fail(
            "the sequence of '" + path + "' mixes values and messages: one sequence is either '"
            + path + "=1.0, 2.0' or '" + path + "[0].' and then a field of it");
        }
        mode = 2;
        if (!parseMapping(elementPath(path, index))) {
          return false;
        }
        ++index;
        continue;
      }
      if (peek() == '[') {
        return fail(
          "'" + elementPath(path, index) +
          "' is a sequence inside a sequence, which has no spelling here");
      }
      if (mode == 2) {
        return fail(
          "the sequence of '" + path + "' mixes messages and values: one sequence is either '"
          + path + "[0].' and then a field of it or '" + path + "=1.0, 2.0'");
      }
      mode = 1;
      if (peek() == '}') {
        return fail("unexpected '}' inside the sequence of '" + path + "'");
      }
      const std::size_t before = pos_;
      std::string item;
      if (peek() == '"' || peek() == '\'') {
        std::size_t pos = pos_;
        if (!decodeQuoted(text_, pos, item, error_)) {
          return false;
        }
        pos_ = pos;
      } else {
        item = parseScalar(true);
      }
      if (pos_ == before) {
        // Nothing was consumed: stopping here is the difference between a
        // clear error and an endless loop that appends an empty element to
        // `joined` until the process runs out of memory.
        return fail(
          "unexpected '" + std::string(1, peek()) + "' inside the sequence of '" + path + "'");
      }
      if (!joined.empty()) {
        joined += ", ";
      }
      joined += item;
      ++index;
    }
    if (mode != 2) {
      emit(path, joined);
    }
    return true;
  }

  // The path of one element of a sequence: `poses`, 2 -> `poses[2]`.
  static std::string elementPath(const std::string & path, std::size_t index)
  {
    return path + "[" + std::to_string(index) + "]";
  }

  // ---- block (unbraced) style -----------------------------------------------
  //
  //   linear:
  //     x: 0.5
  //   angular: {z: -0.2}
  //
  // Entries are separated by the end of a line and, as in the one line
  // `a: 1, b: 2` the demos use, by a `,` that a `key:` follows. A key with
  // nothing after its `:` opens the next block when that block is indented
  // deeper than the key, and is null otherwise - the same reading YAML gives
  // it. `parent_indent` is the indentation of the line that introduced this
  // block (-1 at the top of the document); a line that is not indented deeper
  // than that ends the block and is left for the caller.

  bool parseBlockMapping(const std::string & prefix, int parent_indent)
  {
    pos_ = lineStart(pos_);
    int block_indent = -1;
    while (true) {
      int indent = 0;
      if (!advanceToContent(indent)) {
        break;
      }
      if (indent <= parent_indent) {
        pos_ = lineStart(pos_);
        break;  // dedented: this block is over, the caller reads the line
      }
      if (block_indent < 0) {
        block_indent = indent;
      } else if (indent != block_indent) {
        return fail(
          "bad indentation: every key of '" +
          (prefix.empty() ? std::string("the document") : prefix) +
          "' has to line up with the first one");
      }
      if (!parseBlockEntry(prefix, block_indent)) {
        return false;
      }
    }
    return true;
  }

  // One `key: ...` line of a block, plus the `, key: ...` entries that follow
  // it on the same line.
  bool parseBlockEntry(const std::string & prefix, int block_indent)
  {
    std::string key;
    if (!parseBlockKey(key)) {
      return false;
    }
    const std::string path = join(prefix, key);
    const std::size_t line_end = endOfLine(pos_);

    std::size_t probe = pos_;
    while (probe < line_end && isLineSpace(text_[probe])) {
      ++probe;
    }
    if (probe >= line_end || text_[probe] == '#') {
      // Nothing after the ':': a nested block is indented under this key, or
      // the field is null.
      const std::size_t after_key = nextLine(line_end);
      pos_ = after_key;
      int child = 0;
      if (advanceToContent(child) && child > block_indent) {
        return parseBlockMapping(path, block_indent);
      }
      pos_ = after_key;
      emit(path, std::string());
      return true;
    }

    pos_ = probe;
    if (!parseValue(path, false)) {
      return false;
    }
    if (pos_ > line_end) {
      pos_ = nextLine(pos_);  // a `{...}` value ran over its line
      return true;
    }
    std::size_t after = pos_;
    while (after < line_end && isLineSpace(text_[after])) {
      ++after;
    }
    if (after < line_end && text_[after] == ',') {
      std::size_t next = after + 1;
      while (next < line_end && isLineSpace(text_[next])) {
        ++next;
      }
      if (next < line_end && text_[next] != '#') {
        pos_ = next;
        return parseBlockEntry(prefix, block_indent);
      }
    }
    if (after < line_end && text_[after] != '#') {
      return fail(
        "unexpected '" + trim(text_.substr(after, line_end - after)) +
        "' after the value of '" + path + "'");
    }
    pos_ = nextLine(after);
    return true;
  }

  // The `key:` part of a block entry. The colon that counts is the first one
  // followed by a space or by the end of the line, so the second colon of
  // `data: http://x` is part of the value.
  bool parseBlockKey(std::string & key)
  {
    if (text_[pos_] == '"' || text_[pos_] == '\'') {
      std::size_t pos = pos_;
      if (!decodeQuoted(text_, pos, key, error_)) {
        return false;
      }
      while (pos < text_.size() && isLineSpace(text_[pos])) {
        ++pos;
      }
      if (pos >= text_.size() || text_[pos] != ':') {
        return fail("expected ':' after the key '" + key + "'");
      }
      pos_ = pos + 1;
      return true;
    }
    const std::size_t line_end = endOfLine(pos_);
    std::size_t colon = std::string::npos;
    for (std::size_t i = pos_; i < line_end; ++i) {
      if (text_[i] == ':' && (i + 1 >= line_end || isLineSpace(text_[i + 1]))) {
        colon = i;
        break;
      }
    }
    if (colon == std::string::npos) {
      return fail(
        "expected ':' after the key '" + trim(text_.substr(pos_, line_end - pos_)) + "'");
    }
    key = trim(text_.substr(pos_, colon - pos_));
    if (key.empty()) {
      return fail("expected a key before ':'");
    }
    pos_ = colon + 1;
    return true;
  }

  // Moves to the first character of the next line that has something on it,
  // skipping blank lines and whole-line comments, and reports that line's
  // indentation. False at the end of the text. `pos_` has to be on a line
  // start - every caller leaves it there, and the one place that does not
  // (the end of a `{...}` value that ran over its line) re-syncs itself.
  bool advanceToContent(int & indent)
  {
    while (true) {
      std::size_t i = pos_;
      indent = 0;
      while (i < text_.size() && isLineSpace(text_[i])) {
        if (text_[i] != '\r') {
          ++indent;
        }
        ++i;
      }
      if (i >= text_.size()) {
        pos_ = i;
        return false;
      }
      if (text_[i] == '\n') {
        pos_ = i + 1;
        continue;
      }
      if (text_[i] == '\r' && i + 1 < text_.size() && text_[i + 1] == '\n') {
        pos_ = i + 2;
        continue;
      }
      if (text_[i] == '#') {
        pos_ = nextLine(i);
        continue;
      }
      pos_ = i;
      return true;
    }
  }

  std::size_t lineStart(std::size_t pos) const
  {
    if (pos == 0) {
      return 0;
    }
    const std::size_t newline = text_.rfind('\n', pos - 1);
    return newline == std::string::npos ? 0 : newline + 1;
  }

  std::size_t endOfLine(std::size_t pos) const
  {
    const std::size_t newline = text_.find('\n', pos);
    return newline == std::string::npos ? text_.size() : newline;
  }

  std::size_t nextLine(std::size_t pos) const
  {
    const std::size_t newline = text_.find('\n', pos);
    return newline == std::string::npos ? text_.size() : newline + 1;
  }

  std::string text_;
  std::size_t pos_ = 0;
  std::string prefix_;
  std::vector<FieldAssign> * out_ = nullptr;
  std::string error_;
};

// Flattens `text` (one YAML document) into `out`, nesting everything under
// `prefix` (`linear` + `{x: 0.5}` -> `linear.x=0.5`; empty prefix = top level).
inline bool parseDocumentUnder(
  const std::string & prefix, const std::string & text, std::vector<FieldAssign> & out,
  std::string & error)
{
  Parser parser(text, prefix, &out);
  if (!parser.parse()) {
    error = parser.error();
    return false;
  }
  return true;
}

inline bool parseDocument(
  const std::string & text, std::vector<FieldAssign> & out, std::string & error)
{
  return parseDocumentUnder(std::string(), text, out, error);
}

// Decodes one quoted scalar (`"hi"` or `'hi'`) with the same escapes the
// document parser uses.
inline bool decodeQuotedScalar(
  const std::string & text, std::string & out, std::string & error)
{
  std::size_t pos = 0;
  while (pos < text.size() && isValueSpace(text[pos])) {
    ++pos;
  }
  if (!decodeQuoted(text, pos, out, error)) {
    return false;
  }
  while (pos < text.size() && isValueSpace(text[pos])) {
    ++pos;
  }
  if (pos < text.size()) {
    error = "unexpected '" + text.substr(pos) + "' after the quoted value";
    return false;
  }
  return true;
}

// One `field.path=value` token. A quoted value loses one level of quotes
// (`data="hi"` used to publish the quotes themselves), and a `{...}` / `[...]`
// value nests under the field instead of replacing it.
inline bool parseFlatAssignment(
  const std::string & token, std::vector<FieldAssign> & out, std::string & error)
{
  const std::size_t equals = token.find('=');
  const std::string path = trim(token.substr(0, equals));
  const std::string value = trim(token.substr(equals + 1));
  if (path.empty()) {
    error = "missing a field name before '=' in '" + token + "'";
    return false;
  }
  if (!value.empty() && (value.front() == '{' || value.front() == '[')) {
    // `linear={x: 1}` nests under the field, `ranges=[1, 2]` is that field's
    // whole value - both are documents in their own right, just with a
    // different top level shape.
    Parser parser(value, path, &out);
    const bool parsed = value.front() == '{' ? parser.parse() : parser.parseSequenceValue(path);
    if (!parsed) {
      error = parser.error();
      return false;
    }
    return true;
  }
  if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'')) {
    std::string decoded;
    std::string decode_error;
    if (decodeQuotedScalar(value, decoded, decode_error)) {
      out.push_back(FieldAssign{path, decoded});
      return true;
    }
    // A lone quote inside a string is not an error here: the flat form is also
    // the "just give me the bytes" form.
  }
  out.push_back(FieldAssign{path, value});
  return true;
}

}  // namespace yaml_assign
}  // namespace ros2_bridge

#endif  // ROS2_BRIDGE_YAML_ASSIGN_HPP
