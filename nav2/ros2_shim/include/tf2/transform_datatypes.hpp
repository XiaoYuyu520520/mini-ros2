#ifndef TF2__TRANSFORM_DATATYPES_HPP_
#define TF2__TRANSFORM_DATATYPES_HPP_

#include <string>

#include "tf2/time.hpp"

namespace tf2
{

template<class T>
class Stamped : public T
{
public:
  TimePoint stamp_;
  std::string frame_id_;

  Stamped() = default;

  Stamped(const T & input, const TimePoint & timestamp, const std::string & frame_id)
  : T(input), stamp_(timestamp), frame_id_(frame_id)
  {
  }

  void setData(const T & input) {static_cast<T &>(*this) = input;}
};

}  // namespace tf2

#endif  // TF2__TRANSFORM_DATATYPES_HPP_
