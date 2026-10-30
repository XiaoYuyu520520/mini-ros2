#include "ompl/base/spaces/ReedsSheppStateSpace.h"

namespace ompl
{
namespace base
{
detail::Se2Solution ReedsSheppStateSpace::shortestPath(const State * from, const State * to) const
{
  detail::Se2Solution best = dubins_.shortestPath(from, to);

  // The shortest forward path from the goal back to the start, driven from its
  // far end, is the shortest way of reaching the goal in reverse: it starts at
  // `from` (the far end of that path) and finishes at `to` (its beginning),
  // covering the same curves in the opposite direction.
  const detail::Se2Solution backward =
    detail::reverseSe2Solution(dubins_.shortestPath(to, from));

  if (backward.length < best.length) {
    best = backward;
  }
  return best;
}

double ReedsSheppStateSpace::distance(const State * from, const State * to) const
{
  return shortestPath(from, to).length;
}

void ReedsSheppStateSpace::interpolate(
  const State * from, const State * to, double t, State * state) const
{
  const detail::Se2Solution solution = shortestPath(from, to);
  double travelled = t * solution.length;
  if (travelled < 0.0) {
    travelled = 0.0;
  }
  if (travelled > solution.length) {
    travelled = solution.length;
  }

  double pose[3];
  detail::se2PoseAt(solution.path, solution.start, travelled, pose);
  state->reals[0] = pose[0];
  state->reals[1] = pose[1];
  state->reals[2] = pose[2];
}

ReedsSheppStateSpace::ReedsSheppPath ReedsSheppStateSpace::reedsShepp(
  const State * from, const State * to) const
{
  const detail::Se2Solution solution = shortestPath(from, to);

  ReedsSheppPath path;
  for (int i = 0; i < solution.path.count && i < 5; ++i) {
    const detail::Se2Leg & leg = solution.path.leg[i];
    // The sign of an entry is the direction it is driven: an arc reports its
    // heading change, which is negative when the robot backs round it, and a
    // straight reports the distance, negative when it backs along it.
    path.length_[i] = leg.turn != 0.0 ? leg.turn : leg.length;
  }
  return path;
}

}  // namespace base
}  // namespace ompl
