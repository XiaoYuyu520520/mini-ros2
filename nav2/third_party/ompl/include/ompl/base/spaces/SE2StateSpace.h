#ifndef OMPL_BASE_SPACES_SE2STATE_SPACE_H
#define OMPL_BASE_SPACES_SE2STATE_SPACE_H

#include <cmath>

#include "ompl/base/spaces/detail/Se2Path.h"

namespace ompl
{
namespace base
{

/// Poses in the plane with no motion constraint: distance is the euclidean
/// one and interpolation is linear. This is the heuristic space for the
/// holonomic lattice configuration.
class SE2StateSpace : public StateSpace
{
public:
  SE2StateSpace()
  : StateSpace(3)
  {
  }

  double distance(const State * from, const State * to) const override
  {
    return std::hypot(
      to->reals[0] - from->reals[0],
      to->reals[1] - from->reals[1]);
  }

  void interpolate(
    const State * from, const State * to, double t, State * state) const override
  {
    const double delta = detail::mod2pi(to->reals[2] - from->reals[2]);
    state->reals[0] = from->reals[0] + t * (to->reals[0] - from->reals[0]);
    state->reals[1] = from->reals[1] + t * (to->reals[1] - from->reals[1]);
    state->reals[2] = from->reals[2] + t * (delta > detail::kSe2Pi ? delta - 2.0 * detail::kSe2Pi : delta);
  }
};

}  // namespace base
}  // namespace ompl

#endif  // OMPL_BASE_SPACES_SE2STATE_SPACE_H
