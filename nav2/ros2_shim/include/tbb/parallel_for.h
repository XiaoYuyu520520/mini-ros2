#ifndef TBB__PARALLEL_FOR_H_
#define TBB__PARALLEL_FOR_H_

// Serial stand-in for oneTBB's parallel_for; see parallel_for_each.h for why the
// shim does not link the real library. The body is invoked once with the whole
// range, which is the minimum oneTBB guarantees as well.

#include "tbb/blocked_range.h"

namespace tbb
{

template<typename IndexT, typename BodyT>
void parallel_for(IndexT first, IndexT last, BodyT & body)
{
  body(blocked_range<IndexT>(first, last));
}

template<typename RangeT, typename BodyT>
void parallel_for(RangeT & range, BodyT & body)
{
  body(range);
}

template<typename RangeT, typename BodyT>
void parallel_for(const RangeT & range, const BodyT & body)
{
  body(range);
}

template<typename IndexT, typename BodyT>
void parallel_for(IndexT first, IndexT last, const BodyT & body)
{
  body(blocked_range<IndexT>(first, last));
}

}  // namespace tbb

#endif  // TBB__PARALLEL_FOR_H_
