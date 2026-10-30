# -*- coding: utf-8 -*-
import io

p = r"E:\DesktopData\app\code\CPU\nav2\nav2_extracted\navigation2-1.5.1\nav2_smac_planner\src\collision_checker.cpp"
text = io.open(p, encoding="utf-8", newline="").read()

old = """  geometry_msgs::msg::Point new_pt;
  // `angle_bin` is a node pose theta, passed down by every caller as a bin - but
  // nothing guarantees it is one that fits this table. The analytic expansion derives
  // it from an interpolated yaw, so a heading a whole turn out of range arrives here as
  // a bin a whole table out of range. Indexing past the table is not a rounding error:
  // the size() read two lines below is then whatever is in memory at that address, which
  // becomes a reserve() of gigabytes - the std::bad_alloc that planning reported for a
  // goal it could not reach. Wrap it, and say so, because being handed a bin outside the
  // table means a node pose was filled by something that did not wrap its heading.
  const std::size_t bin_count = oriented_footprints_.size();
  std::size_t bin = 0u;
  if (bin_count > 0u) {
    const double wrapped = std::isfinite(angle_bin) ?
      std::fmod(static_cast<double>(angle_bin), static_cast<double>(bin_count)) : 0.0;
    bin = static_cast<std::size_t>(
      wrapped < 0.0 ? wrapped + static_cast<double>(bin_count) : wrapped);
    bin = std::min(bin, bin_count - 1u);
  }
  if (!(angle_bin >= 0.0f && angle_bin < static_cast<float>(bin_count))) {
    RCLCPP_WARN_THROTTLE(
      logger_, *clock_, 1000,
      "Collision check was given angle bin %f, but there are only %zu oriented footprints;"
      " using bin %zu instead.",
      static_cast<double>(angle_bin), bin_count, bin);
  }
  const nav2_costmap_2d::Footprint & oriented_footprint = oriented_footprints_[bin];
  nav2_costmap_2d::Footprint current_footprint;
  current_footprint.reserve(oriented_footprint.size());
"""

new = """    geometry_msgs::msg::Point new_pt;
    // `angle_bin` is a node pose theta, passed down by every caller as a bin - but
    // nothing guarantees it is one that fits this table. The analytic expansion derives
    // it from an interpolated yaw, so a heading a whole turn out of range arrives here
    // as a bin a whole table out of range. Indexing past the table is not a rounding
    // error: the size() read two lines below is then whatever is in memory at that
    // address, which becomes a reserve() of gigabytes - the std::bad_alloc that
    // planning reported for a goal it could not reach. Wrap it, and say so, because
    // being handed a bin outside the table means a node pose was filled by something
    // that did not wrap its heading.
    const std::size_t bin_count = oriented_footprints_.size();
    std::size_t bin = 0u;
    if (bin_count > 0u) {
      const double wrapped = std::isfinite(angle_bin) ?
        std::fmod(static_cast<double>(angle_bin), static_cast<double>(bin_count)) : 0.0;
      bin = static_cast<std::size_t>(
        wrapped < 0.0 ? wrapped + static_cast<double>(bin_count) : wrapped);
      bin = std::min(bin, bin_count - 1u);
    }
    if (!(angle_bin >= 0.0f && angle_bin < static_cast<float>(bin_count))) {
      RCLCPP_WARN_THROTTLE(
        logger_, *clock_, 1000,
        "Collision check was given angle bin %f, but there are only %zu oriented footprints;"
        " using bin %zu instead.",
        static_cast<double>(angle_bin), bin_count, bin);
    }
    const nav2_costmap_2d::Footprint & oriented_footprint = oriented_footprints_[bin];
    nav2_costmap_2d::Footprint current_footprint;
    current_footprint.reserve(oriented_footprint.size());
"""

print("old count:", text.count(old))
assert text.count(old) == 1
text = text.replace(old, new)
io.open(p, "w", encoding="utf-8", newline="").write(text)
print("rewritten")
