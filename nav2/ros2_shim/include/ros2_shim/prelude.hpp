#ifndef ROS2_SHIM__PRELUDE_HPP_
#define ROS2_SHIM__PRELUDE_HPP_

// Headers that the real ROS2 toolchain pulls in transitively. Forced-included
// (-include) for nav2 translation units to keep upstream sources unmodified.
#include <algorithm>
#include <cassert>
#include <cmath>
// `math.h` as well as `<cmath>`, which is not redundant on glibc. Upstream
// nav2 calls abs() on doubles without qualifying it --
// nav2_graceful_controller/src/graceful_controller.cpp:450 does
// `std::max(abs(vel.angular.z), params_->v_angular_min_in_place)`. With only
// <cmath> and <algorithm> in scope that call resolves to `::abs(int)`, because
// <algorithm> seeds the name lookup with the C declaration, and std::max then
// has an int and a double to deduce from: "no matching function for call to
// max(int, double&)". The real ROS2 toolchain never sees this because <math.h>
// reaches the translation unit transitively; the shim's include set did not.
#include <math.h>
#include <fstream>
#include <iostream>
#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif
#ifndef M_PI_4
#define M_PI_4 0.78539816339744830962
#endif
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// `std::atomic<std::shared_ptr<T>>` is C++20's P0718R2. libstdc++ only
// implements it from GCC 12 on, and Ubuntu 22.04 ships libstdc++ 11 - so the
// instantiation
//
//   std::atomic<std::shared_ptr<std::vector<geometry_msgs::msg::Point>>> footprint_;
//
// in nav2_costmap_2d's layered_costmap.hpp fails there with "std::atomic
// requires a trivially copyable type". Upstream nav2 uses it, and upstream
// nav2 is not modified.
//
// The specialisation below gives those two members the two operations
// upstream actually calls - `store()` and `load()` - with the same semantics
// (a shared_ptr read or written atomically). The mutex is what a
// pre-P0718R2 implementation had to use anyway.
//
// The feature-test macro is the standard's own answer to "does my library
// have this?", so the specialisation is only compiled where it is needed.
#if defined(__cpp_lib_atomic_shared_ptr)
#  define ROS2_SHIM_HAS_ATOMIC_SHARED_PTR 1
#else
#  define ROS2_SHIM_HAS_ATOMIC_SHARED_PTR 0
#endif

#if !ROS2_SHIM_HAS_ATOMIC_SHARED_PTR
#  include <atomic>
#  include <mutex>
namespace std
{
template<class T>
struct atomic<std::shared_ptr<T>>
{
  using value_type = std::shared_ptr<T>;

  atomic() = default;
  explicit atomic(value_type initial) {data_ = std::move(initial);}

  void store(value_type desired, std::memory_order = std::memory_order_seq_cst)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    data_ = std::move(desired);
  }

  value_type load(std::memory_order = std::memory_order_seq_cst) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return data_;
  }

  value_type exchange(value_type desired, std::memory_order = std::memory_order_seq_cst)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    value_type previous = data_;
    data_ = std::move(desired);
    return previous;
  }

  operator value_type() const {return load();}

private:
  mutable std::mutex mutex_;
  value_type data_;
};
}  // namespace std
#endif  // !ROS2_SHIM_HAS_ATOMIC_SHARED_PTR

// The shim's graph header pulls in <windows.h>, whose macro soup collides with
// identifiers that ROS2 messages, upstream nav2 and the rclcpp shim use:
//   wingdi.h (ERROR/TRANSPARENT) -> CallbackReturn::ERROR, ...
//   winnt.h  (DELETE/IN/OUT/OPTIONAL)
//   windef.h (near/far/min/max)
// Pull Windows in here, the leanest way, while the include guard still lets us
// choose how it is expanded, and drop the macros that leak through anyway.
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  ifdef ERROR
#    undef ERROR
#  endif
#  ifdef DELETE
#    undef DELETE
#  endif
#  ifdef TRANSPARENT
#    undef TRANSPARENT
#  endif
#  ifdef IN
#    undef IN
#  endif
#  ifdef OUT
#    undef OUT
#  endif
#  ifdef OPTIONAL
#    undef OPTIONAL
#  endif
#  ifdef near
#    undef near
#  endif
#  ifdef far
#    undef far
#  endif
#endif

#endif  // ROS2_SHIM__PRELUDE_HPP_
