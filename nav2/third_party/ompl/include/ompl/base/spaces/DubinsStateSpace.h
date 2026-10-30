#ifndef OMPL_BASE_SPACES_DUBINS_STATE_SPACE_H
#define OMPL_BASE_SPACES_DUBINS_STATE_SPACE_H

#include <vector>

#include "ompl/base/spaces/detail/Se2Path.h"

namespace ompl
{
namespace base
{

/// Dubins' car: it only drives forward, and cannot turn tighter than a fixed
/// radius. The shortest path between two poses is one of six words - three
/// turns in one direction with a straight between them, or three arcs - and
/// this class solves them in closed form.
///
/// Each solved word is checked by driving its own path forward and comparing
/// where it lands with the requested pose; only words that land on it are
/// offered as candidates.
class DubinsStateSpace : public StateSpace
{
public:
  /// The six words, in the order `solveWords()` reports them.
  enum Word
  {
    LSL = 0,  ///< left arc, straight, left arc
    LSR,      ///< left arc, straight, right arc
    RSL,      ///< right arc, straight, left arc
    RSR,      ///< right arc, straight, right arc
    RLR,      ///< right, left, right arcs
    LRL       ///< left, right, left arcs
  };

  static constexpr int kWordCount = 6;

  /// Solution of one word, in the frame of the starting pose.
  struct WordSolution
  {
    int word{0};
    detail::Se2Path path;
    double length{0.0};
  };

  explicit DubinsStateSpace(double turningRadius = 1.0)
  : StateSpace(3), rho_(turningRadius > 0.0 ? turningRadius : 1.0)
  {
  }

  double getTurningRadius() const {return rho_;}

  double distance(const State * from, const State * to) const override;

  void interpolate(
    const State * from, const State * to, double t, State * state) const override;

  /// The shortest path, with the pose it leaves from. The path is empty when
  /// the two states coincide.
  detail::Se2Solution shortestPath(const State * from, const State * to) const;

  /// Every word that is both feasible and lands on the goal, in `Word` order.
  /// The unit tests use this to check each word individually instead of only
  /// the minimum.
  std::vector<WordSolution> solveWords(const State * from, const State * to) const;

private:
  double rho_;
};

}  // namespace base
}  // namespace ompl

#endif  // OMPL_BASE_SPACES_DUBINS_STATE_SPACE_H
