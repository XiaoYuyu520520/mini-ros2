#ifndef TBB__PARALLEL_FOR_EACH_H_
#define TBB__PARALLEL_FOR_EACH_H_

// Serial stand-in for the single oneTBB algorithm the Karto SDK uses.
//
// Karto parallelises its scan matcher across the y columns of the search window
// (`tbb::parallel_for_each(m_yPoses, *this)` in Mapper.cpp). oneTBB is only
// available on Windows as libtbb12.dll, which would defeat this port's goal of
// self-contained static executables, so the algorithm runs inline instead. Each
// invocation writes a disjoint slice of the pose-response array, so the result
// is identical to the threaded version -- only wall-clock time differs.
//
// The body is taken by reference rather than by value as oneTBB does: Karto's
// functor writes through a raw pointer into the shared pose-response array, so
// handing it a copy would buy nothing here.

#include "tbb/blocked_range.h"

namespace tbb
{

template<typename ContainerT, typename BodyT>
void parallel_for_each(ContainerT & container, BodyT & body)
{
  for (auto & item : container) {
    body(item);
  }
}

template<typename ContainerT, typename BodyT>
void parallel_for_each(const ContainerT & container, const BodyT & body)
{
  for (const auto & item : container) {
    body(item);
  }
}

}  // namespace tbb

#endif  // TBB__PARALLEL_FOR_EACH_H_
