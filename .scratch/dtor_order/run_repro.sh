set -u
D=/tmp/dtor_order_$$
mkdir -p "$D"
cat > "$D/repro.cpp" <<'CPPEOF'
// The destruction-order mechanism behind slam_toolbox-thread-shutdown.patch.
//
// `SynchronousSlamToolbox` (derived) starts a thread in on_activate() that locks
// `q_mutex_` - a member of the *derived* class - on every iteration
// (third_party/slam_toolbox/src/slam_toolbox_sync.cpp:44). The interrupt and
// join live in the *base* destructor (src/slam_toolbox_common.cpp:243-251).
//
// A C++ destructor runs derived body, derived members, base body. So upstream's
// empty `~SynchronousSlamToolbox() {}` destroys q_mutex_ first and joins the
// thread that keeps locking it afterwards. On glibc pthread_mutex_destroy
// leaves __kind == -1, the next pthread_mutex_lock returns EINVAL, boost turns
// that into lock_error, and an exception escaping a destructor is
// std::terminate.
//
// This is the same shape with the same classes and the same two arms:
//   -DWITH_FIX absent -> upstream's empty body
//   -DWITH_FIX        -> the port's body: interrupt and join here first
#include <boost/thread.hpp>
#include <cstdio>
#include <memory>
#include <vector>

struct Base {
  virtual ~Base() {
    for (auto & t : threads_) { t->interrupt(); t->join(); }
    threads_.clear();
  }
  std::vector<std::shared_ptr<boost::thread>> threads_;
};

struct Derived : Base {
  boost::mutex q_mutex_;
  Derived() {
    threads_.push_back(std::make_shared<boost::thread>(
      boost::bind(&Derived::run, this)));
  }
#ifdef WITH_FIX
  ~Derived() {
    for (auto & t : threads_) { t->interrupt(); t->join(); }
    threads_.clear();
  }
#endif
  void run() {
    // The real loop polls at 100 Hz and skips the sleep while the queue has
    // work, so a mapping lap is the case where the thread is spinning through
    // the lock - which is where the abort was measured.
    while (true) {
      boost::this_thread::interruption_point();
      boost::mutex::scoped_lock lock(q_mutex_);
      boost::this_thread::sleep_for(boost::chrono::milliseconds(1));
    }
  }
};

int main() {
  {
    Derived d;
    boost::this_thread::sleep_for(boost::chrono::milliseconds(20));
  }
  std::printf("survived\n");
  return 0;
}
CPPEOF
cd "$D"
LIBS='-lboost_thread -lboost_chrono -lboost_system'
g++ -std=c++17 -O2 -pthread -o repro_upstream repro.cpp $LIBS || exit 1
g++ -std=c++17 -O2 -pthread -DWITH_FIX -o repro_fixed repro.cpp $LIBS || exit 1
for arm in upstream fixed; do
  ok=0; bad=0
  for i in $(seq 1 10); do
    if ./repro_$arm >/dev/null 2>&1; then ok=$((ok+1)); else bad=$((bad+1)); fi
  done
  echo "arm=$arm survived=$ok aborted=$bad"
done
echo '--- one upstream failure, verbatim ---'
./repro_upstream 2>&1 | tail -n 3 || true
cd /; rm -rf "$D"
