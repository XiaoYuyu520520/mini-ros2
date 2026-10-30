#ifndef OMPL_BASE_SPACES_REEDS_SHEPP_STATE_SPACE_H
#define OMPL_BASE_SPACES_REEDS_SHEPP_STATE_SPACE_H

#include "ompl/base/spaces/DubinsStateSpace.h"

namespace ompl
{
namespace base
{

/// Reeds-Shepp's car: Dubins' car, which may also drive in reverse.
///
/// Upstream implements the full 48 word family. This port takes the shorter
/// route of trying the six Dubins words forwards and again on the reversed
/// problem (which is the same path driven backwards with its headings flipped),
/// and keeping whichever is shorter. Every plan it returns is drivable, but one
/// that would need a direction change in the middle of a single word is planned
/// as one reverse leg instead, so it can be a little longer than upstream's.
/// `third_party/ompl/README.md` records the trade-off.
///
/// Deliberately *not* derived from `DubinsStateSpace`: nav2_smac_planner tells
/// the two apart with `dynamic_cast<ReedsSheppStateSpace *>`.
class ReedsSheppStateSpace : public StateSpace
{
public:
  /// The signed lengths of the legs, as upstream exposes them: a negative
  /// entry means that leg is driven backwards.
  struct ReedsSheppPath
  {
    double length_[5]{0.0, 0.0, 0.0, 0.0, 0.0};
  };

  /// OMPL 2.0 renamed this; nav2_smac_planner names both under an `#if`.
  using PathType = ReedsSheppPath;

  explicit ReedsSheppStateSpace(double turningRadius = 1.0)
  : StateSpace(3), dubins_(turningRadius), rho_(turningRadius > 0.0 ? turningRadius : 1.0)
  {
  }

  double getTurningRadius() const {return rho_;}

  double distance(const State * from, const State * to) const override;

  void interpolate(
    const State * from, const State * to, double t, State * state) const override;

  detail::Se2Solution shortestPath(const State * from, const State * to) const;

  ReedsSheppPath reedsShepp(const State * from, const State * to) const;

private:
  DubinsStateSpace dubins_;
  double rho_;
};

}  // namespace base
}  // namespace ompl

#endif  // OMPL_BASE_SPACES_REEDS_SHEPP_STATE_SPACE_H
