#ifndef OMPL_BASE_SCOPED_STATE_H
#define OMPL_BASE_SCOPED_STATE_H

#include <vector>

#include "ompl/base/StateSpace.h"

namespace ompl
{
namespace base
{

/// An owned state, freed when the object goes out of scope.
///
/// The template parameter mirrors upstream's signature - callers write
/// `ScopedState<>` - but the storage does not depend on it.
template<class Space = StateSpace>
class ScopedState
{
public:
  explicit ScopedState(const StateSpacePtr & space)
  : space_(space), state_(space->allocState())
  {
  }

  ScopedState(const ScopedState &) = delete;
  ScopedState & operator=(const ScopedState &) = delete;

  ~ScopedState()
  {
    space_->freeState(state_);
  }

  double & operator[](unsigned int index) {return state_->reals[index];}
  double operator[](unsigned int index) const {return state_->reals[index];}

  State * operator()() {return state_;}
  const State * operator()() const {return state_;}

  State * get() {return state_;}
  const State * get() const {return state_;}

  std::vector<double> reals() const {return state_->reals;}

private:
  StateSpacePtr space_;
  State * state_;
};

}  // namespace base
}  // namespace ompl

#endif  // OMPL_BASE_SCOPED_STATE_H
