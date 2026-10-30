#ifndef TBB__BLOCKED_RANGE_H_
#define TBB__BLOCKED_RANGE_H_

// Minimal blocked_range for the serial oneTBB stand-in. oneTBB would split this
// across worker threads; here it is only a value type, so it never splits.

#include <cstddef>

namespace tbb
{

template<typename Value>
class blocked_range
{
public:
  using const_iterator = Value;
  using size_type = std::size_t;

  blocked_range(Value begin, Value end, size_type /*grainsize*/ = 1)
  : begin_(begin), end_(end)
  {
  }

  Value begin() const {return begin_;}
  Value end() const {return end_;}
  size_type size() const {return static_cast<size_type>(end_ - begin_);}
  bool empty() const {return begin_ == end_;}
  bool is_divisible() const {return false;}

private:
  Value begin_;
  Value end_;
};

}  // namespace tbb

#endif  // TBB__BLOCKED_RANGE_H_
