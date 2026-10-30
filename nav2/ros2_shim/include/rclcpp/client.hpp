#ifndef RCLCPP__CLIENT_HPP_
#define RCLCPP__CLIENT_HPP_

#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/macros.hpp"
#include "rclcpp/qos.hpp"
#include "ros2_shim/graph.hpp"
#include "ros2_shim/serialization.hpp"
#include "shm_transport/shm_channel.hpp"

namespace rclcpp
{

class ClientBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(ClientBase)

  virtual ~ClientBase() = default;
  virtual const char * get_service_name() const = 0;
  virtual void poll() = 0;
};

/// What `async_send_request()` hands back: the future, plus the id the request
/// went out with, so a caller that gives up can hand the id back to
/// `remove_pending_request()` instead of leaving the promise behind.
///
/// Upstream `rclcpp` returns this rather than a bare future, and the ported
/// `nav2_ros_common::ServiceClient` uses it as one: `async_call()` calls
/// `.share()` on it, while `invoke()` passes it to
/// `spin_until_future_complete()` and then calls `.get()`.
template<class T>
class FutureAndRequestId
{
public:
  using SharedFuture = std::shared_future<T>;

  FutureAndRequestId() = default;

  FutureAndRequestId(SharedFuture future, int64_t request_id)
  : future_(std::move(future)), request_id_(request_id)
  {
  }

  SharedFuture share() const {return future_;}
  operator SharedFuture() const {return future_;}

  bool valid() const {return future_.valid();}
  T get() {return future_.get();}
  void wait() const {future_.wait();}

  template<class RepT, class PeriodT>
  std::future_status wait_for(const std::chrono::duration<RepT, PeriodT> & timeout) const
  {
    return future_.wait_for(timeout);
  }

  template<class ClockT, class DurationT>
  std::future_status wait_until(const std::chrono::time_point<ClockT, DurationT> & deadline) const
  {
    return future_.wait_until(deadline);
  }

  int64_t get_request_id() const {return request_id_;}

private:
  SharedFuture future_;
  int64_t request_id_ = 0;
};

template<class ServiceT>
class Client : public ClientBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(Client<ServiceT>)

  using Request = typename ServiceT::Request;
  using Response = typename ServiceT::Response;
  using RequestSharedPtr = std::shared_ptr<Request>;
  using ResponseSharedPtr = std::shared_ptr<Response>;
  using SharedFuture = std::shared_future<ResponseSharedPtr>;

  Client(
    std::string name, std::shared_ptr<shm_transport::ShmChannel> request_channel,
    std::shared_ptr<shm_transport::ShmChannel> response_channel, uint32_t slot_size)
  : name_(std::move(name)), request_channel_(std::move(request_channel)),
    response_channel_(std::move(response_channel)), buffer_(slot_size)
  {
    // The same reason `Service` seeks: a client numbers its requests from 1, so
    // a response left in the ring by an earlier client would be read back as
    // the answer to this client's first request.
    response_channel_->seek_to_latest();
  }

  const char * get_service_name() const override {return name_.c_str();}

  // There is no discovery in the shim, so "is the service up?" is answered by
  // the graph the server advertised itself in (see ros2_shim/graph.hpp). The
  // shim used to answer `true` unconditionally and sleep out the whole timeout
  // doing it, which cost every `nav2_util::LifecycleServiceClient` call five
  // seconds of dead time and made the lifecycle manager's bringup take longer
  // than any timeout it was given.
  bool service_is_ready() const {return ros2_shim::graph::service_exists(name_);}

  template<class RepT, class PeriodT>
  bool wait_for_service(const std::chrono::duration<RepT, PeriodT> & timeout)
  {
    return ros2_shim::graph::wait_for_service(name_, timeout);
  }

  // No timeout: wait for as long as the service takes to appear.
  bool wait_for_service()
  {
    return ros2_shim::graph::wait_for_service(name_, std::chrono::nanoseconds(-1));
  }

  FutureAndRequestId<ResponseSharedPtr> async_send_request(RequestSharedPtr request)
  {
    const uint64_t id = ++counter_;
    const auto payload = ros2_shim::serialize(*request);
    std::vector<uint8_t> out(sizeof(id) + payload.size());
    std::memcpy(out.data(), &id, sizeof(id));
    std::memcpy(out.data() + sizeof(id), payload.data(), payload.size());
    request_channel_->publish(out.data(), out.size());

    auto promise = std::make_shared<std::promise<ResponseSharedPtr>>();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_[id] = promise;
    }
    return FutureAndRequestId<ResponseSharedPtr>(
      promise->get_future().share(), static_cast<int64_t>(id));
  }

  /// The callback form still hands the future back: upstream does, and a caller
  /// that wants to wait for the answer *and* be called when it lands can.
  template<class CallbackT>
  SharedFuture async_send_request(RequestSharedPtr request, CallbackT && callback)
  {
    const uint64_t id = ++counter_;
    const auto payload = ros2_shim::serialize(*request);
    std::vector<uint8_t> out(sizeof(id) + payload.size());
    std::memcpy(out.data(), &id, sizeof(id));
    std::memcpy(out.data() + sizeof(id), payload.data(), payload.size());
    request_channel_->publish(out.data(), out.size());

    auto promise = std::make_shared<std::promise<ResponseSharedPtr>>();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_[id] = promise;
      pending_callbacks_[id] = std::function<void(ResponseSharedPtr)>(
        std::forward<CallbackT>(callback));
    }
    return promise->get_future().share();
  }

  /// Drop a request that will not be waited for. The response, if it ever
  /// arrives, finds no promise to fulfil and is discarded.
  void remove_pending_request(FutureAndRequestId<ResponseSharedPtr> & request)
  {
    const uint64_t id = static_cast<uint64_t>(request.get_request_id());
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.erase(id);
    pending_callbacks_.erase(id);
  }

  template<class FutureT>
  void remove_pending_request(FutureT &) {}

  void poll() override
  {
    for (;;) {
      size_t size = 0;
      if (!response_channel_->subscribe(buffer_.data(), buffer_.size(), &size, 0)) {
        return;
      }
      if (size < sizeof(uint64_t)) {
        continue;
      }
      uint64_t correlation_id = 0;
      std::memcpy(&correlation_id, buffer_.data(), sizeof(correlation_id));
      auto response = std::make_shared<Response>();
      if (!ros2_shim::deserialize(
          buffer_.data() + sizeof(correlation_id), size - sizeof(correlation_id), *response))
      {
        continue;
      }
      std::shared_ptr<std::promise<ResponseSharedPtr>> promise;
      std::function<void(ResponseSharedPtr)> callback;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pending_.find(correlation_id);
        if (it != pending_.end()) {
          promise = it->second;
          pending_.erase(it);
        }
        auto cit = pending_callbacks_.find(correlation_id);
        if (cit != pending_callbacks_.end()) {
          callback = cit->second;
          pending_callbacks_.erase(cit);
        }
      }
      if (promise) {
        promise->set_value(response);
      }
      if (callback) {
        callback(response);
      }
    }
  }

private:
  std::string name_;
  std::shared_ptr<shm_transport::ShmChannel> request_channel_;
  std::shared_ptr<shm_transport::ShmChannel> response_channel_;
  std::vector<uint8_t> buffer_;
  uint64_t counter_ = 0;
  std::mutex mutex_;
  std::map<uint64_t, std::shared_ptr<std::promise<ResponseSharedPtr>>> pending_;
  std::map<uint64_t, std::function<void(ResponseSharedPtr)>> pending_callbacks_;
};

}  // namespace rclcpp

#endif  // RCLCPP__CLIENT_HPP_
