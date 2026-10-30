#ifndef OMPL_BASE_STATE_SPACE_H
#define OMPL_BASE_STATE_SPACE_H

#include <memory>
#include <vector>

namespace ompl
{
namespace base
{

/// A state is exactly as much as the planners under this shim ask for: a fixed
/// width vector of reals, in the state space's own convention.
struct State
{
  std::vector<double> reals;
};

/// Base class of the three spaces nav2_smac_planner uses.
///
/// Upstream OMPL templates `StateSpace` over a state type and pushes the
/// storage into the derived class. The callers here - `ScopedState`, the
/// distance heuristics, the analytic expansion - only ever read and write
/// `reals()`, so one concrete `State` with a `std::vector<double>` inside
/// covers every use and keeps `allocState()` non-virtual.
class StateSpace
{
public:
  using StateType = State;

  explicit StateSpace(unsigned int dimension)
  : dimension_(dimension)
  {
  }

  virtual ~StateSpace() = default;

  unsigned int getDimension() const {return dimension_;}

  State * allocState() const
  {
    State * state = new State();
    state->reals.assign(dimension_, 0.0);
    return state;
  }

  void freeState(State * state) const {delete state;}

  void copyState(State * destination, const State * source) const
  {
    destination->reals = source->reals;
  }

  void copyToReals(std::vector<double> & reals, const State * source) const
  {
    reals = source->reals;
  }

  void copyFromReals(State * destination, const std::vector<double> & reals) const
  {
    destination->reals = reals;
  }

  /// Length of the shortest path this space allows between two states.
  virtual double distance(const State * from, const State * to) const = 0;

  /// Pose `t` of the way along that shortest path, `t` in [0, 1].
  virtual void interpolate(
    const State * from, const State * to, double t, State * state) const = 0;

protected:
  unsigned int dimension_;
};

using StateSpacePtr = std::shared_ptr<StateSpace>;

}  // namespace base
}  // namespace ompl

#endif  // OMPL_BASE_STATE_SPACE_H
