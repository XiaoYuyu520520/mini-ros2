#ifndef POCO__MUTEX_H_
#define POCO__MUTEX_H_

// Minimal stand-in for the Poco Foundation mutex headers.
//
// slam_toolbox vendors the Karto SDK, and Karto's `Karto.h` guards its
// `Object` reference count with `Poco::FastMutex` / `Poco::FastMutex::ScopedLock`.
// That is the only Poco symbol the whole SLAM stack uses, so pulling the Poco
// Foundation into the build (and then shipping poco-foundation.dll) would be
// pure overhead. std::mutex has the same semantics here.

#include <mutex>

namespace Poco
{

class FastMutex
{
public:
  using ScopedLock = std::lock_guard<FastMutex>;

  void lock() {mutex_.lock();}
  void unlock() {mutex_.unlock();}
  bool tryLock() {return mutex_.try_lock();}

private:
  std::mutex mutex_;
};

using Mutex = FastMutex;

}  // namespace Poco

#endif  // POCO__MUTEX_H_
