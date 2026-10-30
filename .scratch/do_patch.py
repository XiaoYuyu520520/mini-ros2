# -*- coding: utf-8 -*-
import io, os, subprocess

EXE = r"C:\Users\Administrator\AppData\Local\OpenAI\Codex\bin\cdef5aaf3e41ab53\codex.exe"
ROOT = r"E:\DesktopData\app\code\CPU"
S = os.path.join(ROOT, ".scratch")

P1 = '''*** Begin Patch
*** Update File: nav2/nav2_extracted/navigation2-1.5.1/nav2_smac_planner/src/collision_checker.cpp
@@
 #include "nav2_smac_planner/collision_checker.hpp"
 
+#include <algorithm>
+#include <cmath>
+#include <cstddef>
+
 namespace nav2_smac_planner
 {
@@
   geometry_msgs::msg::Point new_pt;
-  const nav2_costmap_2d::Footprint & oriented_footprint = oriented_footprints_[angle_bin];
+  // `angle_bin` is a node pose theta, passed down by every caller as a bin - but
+  // nothing guarantees it is one that fits this table. The analytic expansion derives
+  // it from an interpolated yaw, so a heading a whole turn out of range arrives here as
+  // a bin a whole table out of range. Indexing past the table is not a rounding error:
+  // the size() read two lines below is then whatever is in memory at that address, which
+  // becomes a reserve() of gigabytes - the std::bad_alloc that planning reported for a
+  // goal it could not reach. Wrap it, and say so, because being handed a bin outside the
+  // table means a node pose was filled by something that did not wrap its heading.
+  const std::size_t bin_count = oriented_footprints_.size();
+  std::size_t bin = 0u;
+  if (bin_count > 0u) {
+    const double wrapped = std::isfinite(angle_bin) ?
+      std::fmod(static_cast<double>(angle_bin), static_cast<double>(bin_count)) : 0.0;
+    bin = static_cast<std::size_t>(
+      wrapped < 0.0 ? wrapped + static_cast<double>(bin_count) : wrapped);
+    bin = std::min(bin, bin_count - 1u);
+  }
+  if (!(angle_bin >= 0.0f && angle_bin < static_cast<float>(bin_count))) {
+    RCLCPP_WARN_THROTTLE(
+      logger_, *clock_, 1000,
+      "Collision check was given angle bin %f, but there are only %zu oriented footprints;"
+      " using bin %zu instead.",
+      static_cast<double>(angle_bin), bin_count, bin);
+  }
+  const nav2_costmap_2d::Footprint & oriented_footprint = oriented_footprints_[bin];
   nav2_costmap_2d::Footprint current_footprint;
   current_footprint.reserve(oriented_footprint.size());
*** End Patch'''

P2 = '''*** Begin Patch
*** Update File: nav2/nav2_extracted/navigation2-1.5.1/nav2_smac_planner/include/nav2_smac_planner/analytic_expansion_impl.hpp
@@
 #include <algorithm>
+#include <cmath>
 #include <limits>
@@
       reals = s.reals();
-      // Make sure in range [0, 2PI)
-      theta = (reals[2] < 0.0) ? (reals[2] + 2.0 * M_PI) : reals[2];
-      theta = (theta > 2.0 * M_PI) ? (theta - 2.0 * M_PI) : theta;
-      angle = _ctx->motion_table.getAngle(theta);
+      // Make sure in range [0, 2PI). The interpolated yaw is an absolute angle, not an
+      // offset, so it can be more than one turn outside this range and a single add or
+      // subtract - which is what this used to be - leaves it there. The bin this
+      // produces becomes the pose theta of every node on the expansion, and the
+      // collision checker indexes its precomputed footprints with a pose theta.
+      if (!std::isfinite(reals[0]) || !std::isfinite(reals[1]) || !std::isfinite(reals[2])) {
+        // An interpolation that is not a number cannot describe a pose, so there is
+        // nothing here to expand. Abandon it rather than write it into a node.
+        return AnalyticExpansionNodes();
+      }
+      theta = std::fmod(reals[2], 2.0 * M_PI);
+      if (theta < 0.0) {
+        theta += 2.0 * M_PI;
+      }
+      angle = static_cast<float>(_ctx->motion_table.getAngle(theta));
*** End Patch'''

P3 = '''*** Begin Patch
*** Update File: nav2/nav2_extracted/navigation2-1.5.1/nav2_smac_planner/src/node_hybrid.cpp
@@
     const float & node_heading = node->pose.theta;
     float new_heading = node_heading + proj_motion_model._theta;
 
-    if (new_heading < 0.0) {
+    // fmod, not the single add or subtract this used to be: `node_heading` is only
+    // known to be a bin because the pose says so, and a pose filled by an interpolation
+    // is not (see AnalyticExpansion::getAnalyticPath). Adding one turn to a heading
+    // that is already a turn out leaves it a turn out.
+    new_heading = fmodf(new_heading, num_angle_quantization_float);
+    if (new_heading < 0.0f) {
       new_heading += num_angle_quantization_float;
     }
 
-    if (new_heading >= num_angle_quantization_float) {
-      new_heading -= num_angle_quantization_float;
+    // The heading is also the index into this projection's own deltas, where one
+    // outside the table reads past the end of the row.
+    unsigned int node_bin = 0u;
+    if (node_heading >= 0.0f && node_heading < num_angle_quantization_float) {
+      node_bin = static_cast<unsigned int>(node_heading);
     }
 
     projection_list.emplace_back(
-      delta_xs[i][node_heading] + node->pose.x,
-      delta_ys[i][node_heading] + node->pose.y,
+      delta_xs[i][node_bin] + node->pose.x,
+      delta_ys[i][node_bin] + node->pose.y,
       new_heading, proj_motion_model._turn_dir);
*** End Patch'''

for name, patch in (("patch1", P1), ("patch2", P2), ("patch3", P3)):
    path = os.path.join(S, name + ".patch")
    with io.open(path, "w", encoding="ascii", newline="\n") as fh:
        fh.write(patch)
    out = subprocess.run([EXE, "--codex-run-as-apply-patch", patch],
                         capture_output=True, text=True, cwd=ROOT)
    print("====", name, "rc=%d" % out.returncode)
    print(out.stdout.strip()[-2000:])
    if out.stderr.strip():
        print("STDERR:", out.stderr.strip()[-1500:])
