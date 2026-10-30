// Focused regression test for the ported nav2_navfn_planner::NavFn on the
// Windows shared-memory shim.
//
// It pins down the two-step contract that test_e2e.cpp originally got wrong:
//   1. NavFn::calcNavFnAstar() only propagates the potential field and returns
//      whether the start cell became reachable. It never fills the path.
//   2. the polyline is produced by NavFn::calcPath(), exactly like nav2's own
//      NavfnPlanner::getPlanFromPotential() does.
// It also covers the geometry trap: a lethal wall that spans the whole column
// disconnects start from goal, and NavFn then reports failure instead of a
// degenerate path.
#include "nav2_navfn_planner/navfn.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{

constexpr int kSize = 100;
constexpr int kWallX = 50;
constexpr int kWallGapStartY = 70;  // cells with y >= this are the doorway
constexpr int kStartX = 10, kStartY = 10;
constexpr int kGoalX = 90, kGoalY = 90;
constexpr unsigned char kFreeCost = 0;
constexpr unsigned char kLethalCost = 254;  // nav2_costmap_2d::LETHAL_OBSTACLE
constexpr int kMaxCycles = kSize * 4;       // nav2: max_dimension * factor(4)

int g_failures = 0;

void check(bool condition, const char * expression, int line)
{
  if (!condition) {
    std::printf("  FAIL: %s (%s:%d)\n", expression, __FILE__, line);
    ++g_failures;
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// nav2_costmap_2d cells: 0 == free, 254 == LETHAL_OBSTACLE.
std::vector<unsigned char> make_costmap(int wall_end_y)
{
  std::vector<unsigned char> costmap(static_cast<size_t>(kSize) * kSize, kFreeCost);
  for (int y = 0; y < wall_end_y; ++y) {
    costmap[static_cast<size_t>(y) * kSize + kWallX] = kLethalCost;
  }
  return costmap;
}

struct Plan
{
  bool reached_start = false;  // calcNavFnAstar()
  int extracted = 0;           // calcPath() return value
  int path_len = 0;            // getPathLen() after calcPath()
  std::vector<float> xs;
  std::vector<float> ys;
};

Plan run_navfn(const std::vector<unsigned char> & costmap, bool with_calc_path = true)
{
  nav2_navfn_planner::NavFn planner(kSize, kSize);
  // A nav2 costmap is in ROS encoding and the planner may cross unknown cells:
  // this is what NavfnPlanner::makePlan() passes.
  planner.setCostmap(costmap.data(), /*isROS=*/true, /*allow_unknown=*/true);
  int start[2] = {kStartX, kStartY};
  int goal[2] = {kGoalX, kGoalY};
  planner.setStart(start);
  planner.setGoal(goal);

  Plan plan;
  plan.reached_start = planner.calcNavFnAstar([]() {return false;});
  if (with_calc_path) {
    plan.extracted = planner.calcPath(kMaxCycles);
  }
  plan.path_len = planner.getPathLen();
  if (plan.path_len > 0) {
    const float * path_x = planner.getPathX();
    const float * path_y = planner.getPathY();
    plan.xs.assign(path_x, path_x + plan.path_len);
    plan.ys.assign(path_y, path_y + plan.path_len);
  }
  return plan;
}

bool is_at_cell(float value, int cell)
{
  return std::fabs(value - static_cast<float>(cell)) < 0.51f;
}

// calcNavFnAstar() alone must not be mistaken for a plan; calcPath() produces
// the polyline. This is the bug that made test_e2e.cpp report 0 waypoints.
void test_path_requires_calc_path()
{
  std::printf("[test] calcNavFnAstar() propagates, calcPath() extracts\n");
  const auto costmap = make_costmap(kWallGapStartY);

  nav2_navfn_planner::NavFn planner(kSize, kSize);
  planner.setCostmap(costmap.data(), true, true);
  int start[2] = {kStartX, kStartY};
  int goal[2] = {kGoalX, kGoalY};
  planner.setStart(start);
  planner.setGoal(goal);

  CHECK(planner.getPathLen() == 0);
  CHECK(planner.calcNavFnAstar([]() {return false;}));
  CHECK(planner.getPathLen() == 0);  // potential field only, no polyline yet
  CHECK(planner.calcPath(kMaxCycles) > 0);
  CHECK(planner.getPathLen() > 10);
  std::printf("  pathLen after calcPath = %d\n", planner.getPathLen());
}

// The real regression: a non-trivial plan around a lethal wall.
void test_plans_around_wall()
{
  std::printf("[test] plans a non-trivial path through the doorway\n");
  const auto costmap = make_costmap(kWallGapStartY);
  const Plan plan = run_navfn(costmap);

  CHECK(plan.reached_start);
  CHECK(plan.extracted > 10);
  CHECK(plan.path_len > 10);
  CHECK(plan.path_len == plan.extracted);
  std::printf(
    "  astar=%d calcPath=%d waypoints=%zu\n", plan.reached_start, plan.extracted, plan.xs.size());
  if (plan.xs.size() < 2) {
    return;
  }

  CHECK(is_at_cell(plan.xs.front(), kStartX) && is_at_cell(plan.ys.front(), kStartY));
  CHECK(is_at_cell(plan.xs.back(), kGoalX) && is_at_cell(plan.ys.back(), kGoalY));

  int wall_crossings_outside_doorway = 0;
  int lethal_poses = 0;
  for (size_t i = 0; i < plan.xs.size(); ++i) {
    const long cell_x = std::lround(plan.xs[i]);
    const long cell_y = std::lround(plan.ys[i]);
    if (cell_x == kWallX && cell_y < kWallGapStartY) {
      ++wall_crossings_outside_doorway;
    }
    if (cell_x >= 0 && cell_x < kSize && cell_y >= 0 && cell_y < kSize) {
      if (costmap[static_cast<size_t>(cell_y) * kSize + cell_x] == kLethalCost) {
        ++lethal_poses;
      }
    }
  }
  CHECK(wall_crossings_outside_doorway == 0);
  CHECK(lethal_poses == 0);

  const auto lowest_x = std::min_element(plan.xs.begin(), plan.xs.end());
  const auto highest_x = std::max_element(plan.xs.begin(), plan.xs.end());
  CHECK(*lowest_x < kWallX);   // starts on the left of the wall
  CHECK(*highest_x > kWallX);  // and gets to the right of it

  float longest_step = 0.0f;
  for (size_t i = 1; i < plan.xs.size(); ++i) {
    longest_step = std::max(
      longest_step, std::hypot(plan.xs[i] - plan.xs[i - 1], plan.ys[i] - plan.ys[i - 1]));
  }
  CHECK(longest_step < 1.5f);  // pathStep is 0.5 cell, so no teleporting
  std::printf(
    "  first=(%.1f, %.1f) last=(%.1f, %.1f) detour_x=[%.1f, %.1f] max_step=%.2f\n",
    plan.xs.front(), plan.ys.front(), plan.xs.back(), plan.ys.back(), *lowest_x, *highest_x,
    longest_step);
}

// Without the doorway the goal is genuinely unreachable, and NavFn must say so
// rather than hand back an empty "success".
void test_wall_without_gap_is_unreachable()
{
  std::printf("[test] a wall without a gap is reported as unreachable\n");
  const auto costmap = make_costmap(kSize);  // wall spans the whole column
  const Plan plan = run_navfn(costmap);

  CHECK(!plan.reached_start);
  CHECK(plan.extracted == 0);
  // calcPath() returning 0 is the authoritative "no plan" signal
  // (NavfnPlanner::getPlanFromPotential() bails out on it). npath is not reset
  // on failure - it keeps the single partial cell it pushed before noticing the
  // high potential - so getPathLen() alone must never be used as a verdict.
  CHECK(plan.path_len <= 1);
  std::printf(
    "  astar=%d calcPath=%d getPathLen=%d\n", plan.reached_start, plan.extracted,
    plan.path_len);
}

}  // namespace

int main()
{
  std::printf("=== navfn planning regression test ===\n");
  test_path_requires_calc_path();
  test_plans_around_wall();
  test_wall_without_gap_is_unreachable();
  if (g_failures == 0) {
    std::printf("=== ALL NAVFN PLANNING TESTS PASSED ===\n");
    return 0;
  }
  std::printf("=== %d NAVFN PLANNING CHECK(S) FAILED ===\n", g_failures);
  return 1;
}
