import io, shutil, os

P = "nav2/ros2_shim/include/rclcpp/node.hpp"
shutil.copyfile(P, ".scratch/claim_fix_backup/node.hpp.bak")
size0 = os.path.getsize(P)
text = io.open(P, encoding="utf-8", newline="").read()
assert "\r\n" not in text

# ---------------------------------------------------------------- 1. rule parser
anchor = """// A ROS2 namespace is either empty (the root) or absolute: `/ns`, never `ns/`
// and never `ns`. The shim's name goes into the graph, into every topic and
"""
assert text.count(anchor) == 1
rules_block = """// One `-r <from>:=<to>` rule.
//
// `rcl` keeps these in the order they were given, expands both sides against
// the node's namespace before it compares, and stops at the first rule that
// matches - `rcl_remap.cpp`'s `rcl_remap_topic_name` walks the list and returns
// on the first hit. A rule covers every kind of name at once (topic, service,
// action), because a name is a name: the port resolves all of them through
// `Node::resolve_topic`, and that is where the rules are applied.
//
// This is the mechanism upstream's launch files use to put
// `nav2_velocity_smoother` and `nav2_collision_monitor` between the controller
// and the wheels, which is the whole reason it exists here:
//
//     -r cmd_vel:=cmd_vel_nav
//
// The two special keys `__node` and `__ns` are rules too, but they are read
// where the node's identity is built (`remappedArgument` above) instead of
// here. A rule may name the node it applies to
// (`global_costmap:__ns:=global_costmap`), so only the last colon-separated
// field is kept; the filter is not checked, because the node being built is the
// one the filter names.
struct RemapRule
{
  std::string from;
  std::string to;
};

inline std::vector<RemapRule> remapRules(const std::vector<std::string> & arguments)
{
  std::vector<RemapRule> rules;
  for (std::size_t i = 0; i + 1 < arguments.size(); ++i) {
    if (arguments[i] != "-r" && arguments[i] != "--remap") {
      continue;
    }
    const std::string & rule = arguments[i + 1];
    const std::size_t separator = rule.find(":=");
    if (separator == std::string::npos || separator == 0) {
      continue;
    }
    std::string from = rule.substr(0, separator);
    const std::size_t colon = from.rfind(':');
    if (colon != std::string::npos) {
      from = from.substr(colon + 1);
    }
    if (from.empty() || (from.size() > 1 && from[0] == '_' && from[1] == '_')) {
      continue;
    }
    rules.push_back(RemapRule{from, rule.substr(separator + 2)});
  }
  return rules;
}

"""
text = text.replace(anchor, rules_block + anchor, 1)

# ------------------------------------------------------- 2. member + constructor
old_member = """  std::string name_;
  std::string namespace_;
  NodeOptions options_;
  rclcpp::Logger logger_;"""
new_member = """  std::string name_;
  std::string namespace_;
  NodeOptions options_;
  // The `-r` rules this node resolves names through, in the order they were
  // given (see `detail::remapRules`).
  std::vector<detail::RemapRule> remap_rules_;
  rclcpp::Logger logger_;"""
assert text.count(old_member) == 1
text = text.replace(old_member, new_member, 1)

old_init = """    options_(options), logger_(name_), clock_(std::make_shared<rclcpp::Clock>()),"""
new_init = """    options_(options), remap_rules_(detail::remapRules(options.arguments())),
    logger_(name_), clock_(std::make_shared<rclcpp::Clock>()),"""
assert text.count(old_init) == 1
text = text.replace(old_init, new_init, 1)

# ------------------------------------------------------------- 3. resolve_topic
old_resolve = """  std::string resolve_topic(const std::string & name) const
  {
    if (name.empty()) {
      return "/";
    }
    if (name[0] == '/') {
      return name;
    }
    if (name[0] == '~') {
      const std::string suffix =
        name.size() > 1 && name[1] == '/' ? name.substr(2) : name.substr(1);
      return resolve_topic(name_ + "/" + suffix);
    }
    if (namespace_.empty() || namespace_ == "/") {
      return "/" + name;
    }
    if (namespace_.back() == '/') {
      return namespace_ + name;
    }
    return namespace_ + "/" + name;
  }
"""
new_resolve = """  std::string resolve_topic(const std::string & name) const
  {
    return apply_remap(plain_topic_name(name));
  }

  // The rules from `-r`, applied the way `rcl` applies them: a rule matches when
  // its `from` resolves to the same name, and the first match wins. Expanding
  // both sides is what makes `cmd_vel:=cmd_vel_nav` and
  // `/cmd_vel:=/cmd_vel_nav` the same rule for a node in the root namespace,
  // and what lands a relative `to` in this node's namespace like any other
  // relative name. `to` is resolved before the rules are consulted again, so a
  // chain (`/a:=/b` plus `/b:=/c`) moves one step rather than two - which is
  // also what rcl does.
  std::string apply_remap(const std::string & name) const
  {
    for (const detail::RemapRule & rule : remap_rules_) {
      if (plain_topic_name(rule.from) == name) {
        return plain_topic_name(rule.to);
      }
    }
    return name;
  }

  // The rules this node was built with, for a caller that wants to report them
  // (`ros2 param`-style tooling, and the shim's own tests).
  const std::vector<detail::RemapRule> & remap_rules() const {return remap_rules_;}

  // Everything above, before the `-r` rules: `/x` stays, `rel` gets the
  // namespace in front, and `~` expands against the node's own name.
  std::string plain_topic_name(const std::string & name) const
  {
    if (name.empty()) {
      return "/";
    }
    if (name[0] == '/') {
      return name;
    }
    if (name[0] == '~') {
      const std::string suffix =
        name.size() > 1 && name[1] == '/' ? name.substr(2) : name.substr(1);
      return plain_topic_name(name_ + "/" + suffix);
    }
    if (namespace_.empty() || namespace_ == "/") {
      return "/" + name;
    }
    if (namespace_.back() == '/') {
      return namespace_ + name;
    }
    return namespace_ + "/" + name;
  }
"""
assert text.count(old_resolve) == 1
text = text.replace(old_resolve, new_resolve, 1)

io.open(P, "w", encoding="utf-8", newline="\n").write(text)
out = io.open(P, encoding="utf-8", newline="").read()
assert "\r\n" not in out
assert size0 < os.path.getsize(P)
print("node.hpp patched:", size0, "->", os.path.getsize(P), "bytes")