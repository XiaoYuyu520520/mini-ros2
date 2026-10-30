#ifndef RCLCPP__SERVICE_HPP_
#define RCLCPP__SERVICE_HPP_

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rmw/types.h"
#include "rclcpp/macros.hpp"
#include "rclcpp/qos.hpp"
#include "ros2_shim/graph.hpp"
#include "ros2_shim/serialization.hpp"
#include "shm_transport/shm_channel.hpp"

namespace rclcpp
{

class ServiceBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(ServiceBase)

  virtual ~ServiceBase() = default;
  virtual const char * get_service_name() const = 0;
  virtual void poll() = 0;
};

template<class ServiceT>
class Service : public ServiceBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(Service<ServiceT>)

  using Request = typename ServiceT::Request;
  using Response = typename ServiceT::Response;
  using RequestSharedPtr = std::shared_ptr<Request>;
  using ResponseSharedPtr = std::shared_ptr<Response>;
  using CallbackType = std::function<void(
      std::shared_ptr<rmw_request_id_t>, RequestSharedPtr, ResponseSharedPtr)>;

  Service(
    std::string name, std::shared_ptr<shm_transport::ShmChannel> request_channel,
    std::shared_ptr<shm_transport::ShmChannel> response_channel, CallbackType callback,
    uint32_t slot_size)
  : name_(std::move(name)), request_channel_(std::move(request_channel)),
    response_channel_(std::move(response_channel)), callback_(std::move(callback)),
    buffer_(slot_size)
  {
    // Every reader in the shim attaches at the head of its channel - see
    // `Subscription` and both halves of `rclcpp_action`. A service that did
    // not would start with its cursor at 0 and *replay* whatever requests the
    // ring still holds from an earlier server of the same name: a second
    // `slam_toolbox` node answered a `serialize_map` request whose client had
    // long gone, and its empty graph overwrote the 28 MB archive it was about
    // to deserialize. ROS 2's volatile durability drops requests that predate
    // the server in the same way.
    request_channel_->seek_to_latest();
    advertise();
  }

  // Advertised for exactly as long as the service can answer, so a client's
  // `wait_for_service()` can tell "not up yet" from "gone again" - the whole
  // bringup of the lifecycle manager depends on that answer being true.
  ~Service() override
  {
    ros2_shim::graph::ProcessRegistry::instance().remove_service(name_);
  }

  const char * get_service_name() const override {return name_.c_str();}

  void poll() override
  {
    for (;;) {
      size_t size = 0;
      if (!request_channel_->subscribe(buffer_.data(), buffer_.size(), &size, 0)) {
        return;
      }
      if (size < sizeof(uint64_t)) {
        continue;
      }
      uint64_t correlation_id = 0;
      std::memcpy(&correlation_id, buffer_.data(), sizeof(correlation_id));
      auto request = std::make_shared<Request>();
      if (!ros2_shim::deserialize(
          buffer_.data() + sizeof(correlation_id), size - sizeof(correlation_id), *request))
      {
        continue;
      }
      auto response = std::make_shared<Response>();
      auto header = std::make_shared<rmw_request_id_t>();
      header->sequence_number = static_cast<int64_t>(correlation_id);
      callback_(header, request, response);
      const auto payload = ros2_shim::serialize(*response);
      std::vector<uint8_t> out(sizeof(correlation_id) + payload.size());
      std::memcpy(out.data(), &correlation_id, sizeof(correlation_id));
      std::memcpy(out.data() + sizeof(correlation_id), payload.data(), payload.size());
      response_channel_->publish(out.data(), out.size());
    }
  }

  // The one place a service is announced in the shim graph. Every path into
  // `rclcpp::Service` - `Node::create_service`, `rclcpp::create_service`, and
  // the `~/change_state` family rclcpp_lifecycle makes - ends up here.
  void advertise()
  {
    ros2_shim::graph::ProcessRegistry::instance().add_service(
      name_, ros2_shim::service_type_name<ServiceT>());
  }

private:
  std::string name_;
  std::shared_ptr<shm_transport::ShmChannel> request_channel_;
  std::shared_ptr<shm_transport::ShmChannel> response_channel_;
  CallbackType callback_;
  std::vector<uint8_t> buffer_;
};

}  // namespace rclcpp

#endif  // RCLCPP__SERVICE_HPP_
