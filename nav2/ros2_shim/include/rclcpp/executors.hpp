#ifndef RCLCPP__EXECUTORS_HPP_
#define RCLCPP__EXECUTORS_HPP_

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "rclcpp/node.hpp"
#include "rclcpp/utilities.hpp"

namespace rclcpp
{

enum class FutureReturnCode
{
  SUCCESS = 0,
  INTERRUPTED = 1,
  TIMEOUT = 2,
};

inline const char * to_string(FutureReturnCode code)
{
  switch (code) {
    case FutureReturnCode::SUCCESS: return "SUCCESS";
    case FutureReturnCode::INTERRUPTED: return "INTERRUPTED";
    case FutureReturnCode::TIMEOUT: return "TIMEOUT";
  }
  return "UNKNOWN";
}

class Executor
{
public:
  using SharedPtr = std::shared_ptr<Executor>;

  virtual ~Executor() = default;

  void add_node(node_interfaces::NodeBaseInterface::SharedPtr node_base)
  {
    if (!node_base) {
      return;
    }
    for (auto & existing : nodes_) {
      if (existing.get() == node_base.get()) {
        return;
      }
    }
    nodes_.push_back(std::move(node_base));
  }

  void add_node(rclcpp::Node::SharedPtr node)
  {
    add_node(node->get_node_base_interface());
  }

  template<typename NodeT>
  void add_node(std::shared_ptr<NodeT> node)
  {
    add_node(node->get_node_base_interface());
  }

  void remove_node(node_interfaces::NodeBaseInterface::SharedPtr node_base)
  {
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
      if (it->get() == node_base.get()) {
        nodes_.erase(it);
        return;
      }
    }
  }

  void remove_node(rclcpp::Node::SharedPtr node)
  {
    remove_node(node->get_node_base_interface());
  }

  // An executor is handed either a node - which brings every group that asked
  // to come with it - or one group on its own, and the two drive different
  // things. This is upstream's split, and the internal executors depend on it:
  // `nav2::SimpleActionServer` spins its action server's group on a thread of
  // its own, and `nav2::ServiceClient` spins a group of its own while it waits
  // for a reply - the lifecycle manager's whole bringup runs through one.
  void add_callback_group(
    rclcpp::CallbackGroup::SharedPtr callback_group,
    node_interfaces::NodeBaseInterface::SharedPtr node_base)
  {
    if (!node_base) {
      return;
    }
    if (!callback_group || callback_group->automatically_add_to_executor_with_node()) {
      add_node(std::move(node_base));
      return;
    }
    for (auto & existing : groups_) {
      if (existing.first.get() == node_base.get() &&
        existing.second.get() == callback_group.get())
      {
        return;
      }
    }
    groups_.push_back({std::move(node_base), std::move(callback_group)});
  }

  void remove_callback_group(
    rclcpp::CallbackGroup::SharedPtr callback_group,
    node_interfaces::NodeBaseInterface::SharedPtr node_base)
  {
    for (auto it = groups_.begin(); it != groups_.end(); ++it) {
      if (it->first.get() == node_base.get() && it->second.get() == callback_group.get()) {
        groups_.erase(it);
        return;
      }
    }
  }

  void remove_callback_group(rclcpp::CallbackGroup::SharedPtr callback_group, rclcpp::Node::SharedPtr node)
  {
    remove_callback_group(std::move(callback_group), node->get_node_base_interface());
  }

  virtual void spin_once()
  {
    for (auto & node_base : nodes_) {
      if (node_base && node_base->node) {
        node_base->node->spin_once();
      }
    }
    for (auto & entry : groups_) {
      if (entry.first && entry.first->node) {
        entry.first->node->spin_once(entry.second);
      }
    }
  }

  virtual void spin()
  {
    while (rclcpp::ok() && !cancel_) {
      spin_once();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  void cancel() {cancel_.store(true);}

  void spin_some() {spin_once();}

  void spin_all(const std::chrono::nanoseconds & = std::chrono::nanoseconds(-1)) {spin_once();}

  template<class FutureT>
  FutureReturnCode spin_until_future_complete(
    FutureT & future,
    const std::chrono::nanoseconds & timeout = std::chrono::nanoseconds(-1))
  {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (rclcpp::ok()) {
      spin_once();
      if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        return FutureReturnCode::SUCCESS;
      }
      if (timeout.count() > 0 && std::chrono::steady_clock::now() >= deadline) {
        return FutureReturnCode::TIMEOUT;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return FutureReturnCode::INTERRUPTED;
  }

  template<class FutureT>
  FutureReturnCode spin_until_future_complete(
    node_interfaces::NodeBaseInterface::SharedPtr,
    FutureT & future,
    const std::chrono::nanoseconds & timeout = std::chrono::nanoseconds(-1))
  {
    return spin_until_future_complete(future, timeout);
  }

protected:
  // The nodes whose groups asked to be spun along with them, and the groups
  // that were added on their own - each with the node it belongs to, since a
  // group is only ever driven through its node here.
  std::vector<node_interfaces::NodeBaseInterface::SharedPtr> nodes_;
  std::vector<
    std::pair<node_interfaces::NodeBaseInterface::SharedPtr, rclcpp::CallbackGroup::SharedPtr>>
  groups_;
  std::atomic<bool> cancel_{false};
};

class SingleThreadedExecutor : public Executor
{
public:
  using SharedPtr = std::shared_ptr<SingleThreadedExecutor>;
};

class MultiThreadedExecutor : public Executor
{
public:
  using SharedPtr = std::shared_ptr<MultiThreadedExecutor>;

  explicit MultiThreadedExecutor(size_t num_threads = 0, bool = false)
  : num_threads_(num_threads ? num_threads : 2)
  {
  }

  void spin() override
  {
    std::vector<std::thread> threads;
    for (size_t i = 0; i < num_threads_; ++i) {
      threads.emplace_back([this]() {
          while (rclcpp::ok() && !cancel_) {
            spin_once();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
          }
        });
    }
    for (auto & thread : threads) {
      thread.join();
    }
  }

private:
  size_t num_threads_;
};

namespace executors
{
using Executor = ::rclcpp::Executor;
using SingleThreadedExecutor = ::rclcpp::SingleThreadedExecutor;
using MultiThreadedExecutor = ::rclcpp::MultiThreadedExecutor;
}  // namespace executors

inline void spin(rclcpp::Node::SharedPtr node)
{
  while (rclcpp::ok()) {
    node->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

inline void spin(node_interfaces::NodeBaseInterface::SharedPtr node_base)
{
  while (rclcpp::ok()) {
    if (node_base && node_base->node) {
      node_base->node->spin_once();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

template<typename NodeT>
inline void spin_some(std::shared_ptr<NodeT> node)
{
  node->spin_once();
}

inline void spin_some(rclcpp::Node::SharedPtr node)
{
  node->spin_once();
}

template<typename ResponseT>
inline void spin_until_future_complete(
  rclcpp::Node::SharedPtr node, const std::shared_future<ResponseT> & future,
  const std::chrono::nanoseconds & timeout = std::chrono::nanoseconds(-1))
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (rclcpp::ok()) {
    node->spin_once();
    if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      return;
    }
    if (timeout.count() > 0 && std::chrono::steady_clock::now() >= deadline) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

template<class FutureT>
inline FutureReturnCode spin_until_future_complete(
  node_interfaces::NodeBaseInterface::SharedPtr node_base,
  const FutureT & future,
  const std::chrono::nanoseconds & timeout = std::chrono::nanoseconds(-1))
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (rclcpp::ok()) {
    if (node_base && node_base->node) {
      node_base->node->spin_once();
    }
    if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      return FutureReturnCode::SUCCESS;
    }
    if (timeout.count() > 0 && std::chrono::steady_clock::now() >= deadline) {
      return FutureReturnCode::TIMEOUT;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return FutureReturnCode::INTERRUPTED;
}

}  // namespace rclcpp

#endif  // RCLCPP__EXECUTORS_HPP_
