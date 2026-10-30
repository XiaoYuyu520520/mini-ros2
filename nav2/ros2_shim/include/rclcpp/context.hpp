#ifndef RCLCPP__CONTEXT_HPP_
#define RCLCPP__CONTEXT_HPP_

// The rcl Context, cut down to the one thing the ported code uses it for:
// learning that the process is going away.
//
// nav2 does its teardown in a pre-shutdown callback rather than in a
// destructor - `nav2::LifecycleNode` registers one that runs
// `deactivate()` + `cleanup()` ("runCleanups"), and `nav2::LifecycleManager`
// registers one that drops its bond map and joins its service thread. That
// ordering is not cosmetic: `Costmap2DROS` owns a
// `std::unique_ptr<std::thread>` map-update loop that only `on_deactivate()`
// joins, and a `std::thread` destroyed while still joinable calls
// `std::terminate` - which is what `planner_server` and `controller_server`
// did at the end of a run, the moment a shutdown left their costmaps ACTIVE.
//
// Two upstream details had to match for the callbacks to be of any use here:
//
//   * every node of a process shares one Context (rclcpp's "global default
//     context"), because `rclcpp::shutdown()` has no other way to reach them.
//     The shim used to give each node base interface a private Context, so
//     the callbacks piled up on objects nothing ever shut down;
//   * `remove_pre_shutdown_callback()` has to really remove - a node that was
//     destroyed would otherwise leave a callback pointing at freed memory,
//     waiting for the next `rclcpp::shutdown()` to call it.

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace rclcpp
{

struct PreShutdownCallbackHandle
{
  // 0 means "not registered"; the first real callback gets 1, so a default
  // constructed handle never removes anything.
  std::int64_t id = 0;
};

class Context
{
public:
  using SharedPtr = std::shared_ptr<Context>;
  using PreShutdownCallback = std::function<void()>;

  /// The one context every node in this process is created in.
  static SharedPtr get_global()
  {
    static SharedPtr context = std::make_shared<Context>();
    return context;
  }

  PreShutdownCallbackHandle add_pre_shutdown_callback(PreShutdownCallback callback)
  {
    const std::int64_t id = next_id_++;
    callbacks_.emplace_back(id, std::move(callback));
    return PreShutdownCallbackHandle{id};
  }

  void remove_pre_shutdown_callback(const PreShutdownCallbackHandle & handle)
  {
    for (auto entry = callbacks_.begin(); entry != callbacks_.end(); ++entry) {
      if (entry->first == handle.id) {
        callbacks_.erase(entry);
        return;
      }
    }
  }

  /// Runs every registered callback, in registration order, and returns how
  /// many there were. Only the first call does anything: `rclcpp::shutdown()`
  /// can be called from anywhere - several runners end with it, and a signal
  /// handler would be the third caller - and the callbacks are not
  /// idempotent in the general case (`nav2::LifecycleManager` clears
  /// containers as it goes).
  ///
  /// The list is emptied before the callbacks run, so a callback that
  /// registers another one - or one that destroys nodes - does not invalidate
  /// the iteration.
  std::size_t run_pre_shutdown_callbacks()
  {
    if (ran_) {
      return 0;
    }
    ran_ = true;

    const std::vector<std::pair<std::int64_t, PreShutdownCallback>> pending = callbacks_;
    callbacks_.clear();
    for (const auto & entry : pending) {
      entry.second();
    }
    return pending.size();
  }

private:
  std::vector<std::pair<std::int64_t, PreShutdownCallback>> callbacks_;
  std::int64_t next_id_ = 1;
  bool ran_ = false;
};

}  // namespace rclcpp

#endif  // RCLCPP__CONTEXT_HPP_
