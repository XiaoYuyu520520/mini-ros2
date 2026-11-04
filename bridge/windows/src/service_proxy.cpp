#include "ros2_bridge/service_proxy.hpp"

#include <cstdio>
#include <cstring>

#include "ros2_bridge/shm_topic.hpp"
#include "ros2_shim/graph.hpp"

namespace ros2_bridge
{
namespace
{

bool local_service_exists(const std::string & name)
{
  for (const auto & entry : ros2_shim::graph::list_services()) {
    if (entry.name == name && entry.source == "local") {
      return true;
    }
  }
  return false;
}

}  // namespace

ServiceProxyService::ServiceProxyService(ServiceProxyContext context)
: context_(std::move(context))
{
}

bool ServiceProxyService::sync()
{
  if (!context_.enabled) {
    return true;
  }
  std::vector<ros2_grpc_sdk::ServiceInfo> services;
  const ros2_grpc_sdk::Status status = context_.client->listServices(services);
  if (!status.ok()) {
    if (context_.report_scan_failure) {
      context_.report_scan_failure("listServices", status.message());
    }
    return false;
  }
  if (context_.clear_scan_failure) {
    context_.clear_scan_failure("listServices");
  }

  for (const auto & service : services) {
    if (service.name.empty()) {
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (services_.count(service.name) > 0) {
        continue;
      }
    }
    if (local_service_exists(service.name)) {
      continue;
    }
    if (context_.filters != nullptr && !context_.filters->empty() &&
      !matchesAny(service.name, *context_.filters))
    {
      continue;
    }
    const ServiceCodec * codec = find_service_codec(service.type);
    if (codec == nullptr) {
      // Only worth a line once per type: a robot has a handful of types and
      // hundreds of services (every nav2 node has a `~/change_state`).
      bool already_warned = false;
      if (context_.warned_types) {
        std::lock_guard<std::mutex> lock(mutex_);
        already_warned = context_.warned_types->count(service.type) > 0;
        if (!already_warned) {
          context_.warned_types->insert(service.type);
        }
      }
      if (!already_warned) {
        std::printf(
          "[bridge] skipping '%s': no codec for service type '%s'\n",
          service.name.c_str(), service.type.c_str());
      }
      continue;
    }
    startService(service, codec);
  }
  return true;
}

void ServiceProxyService::startService(
  const ros2_grpc_sdk::ServiceInfo & info, const ServiceCodec * codec)
{
  auto service = std::make_shared<ServiceProxy>();
  service->name = info.name;
  service->type = canonical_service_type(info.type);
  service->codec = codec;

  std::string error;
  // Created rather than opened, and held for as long as the mirror lasts:
  // both channels are owned by whoever is advertised as the remote end, and
  // a `Local\` mapping disappears with its last handle. Keeping them here is
  // what lets a client that shows up an hour later still find them.
  service->requests = create_service_channel(info.name, "request", &error);
  if (!service->requests) {
    std::fprintf(
      stderr, "[bridge] cannot create the request channel for '%s': %s\n",
      info.name.c_str(), error.c_str());
    return;
  }
  service->responses = create_service_channel(info.name, "response", &error);
  if (!service->responses) {
    std::fprintf(
      stderr, "[bridge] cannot create the response channel for '%s': %s\n",
      info.name.c_str(), error.c_str());
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (services_.count(info.name) > 0) {
      return;
    }
    services_[info.name] = service;
  }

  // Advertised as long as the bridge can answer, which is what makes a
  // client's `wait_for_service()` say yes instead of timing out.
  const std::string & peer = context_.peer != nullptr ? *context_.peer : std::string();
  ros2_shim::graph::ProcessRegistry::instance().add_service(
    info.name, service->type, "remote:" + peer);

  if (context_.once) {
    return;
  }
  service->running = true;
  service->worker = std::thread([this, service]() { serviceLoop(service); });
  std::printf(
    "[bridge] serving %s [%s] on shm '%s'\n", info.name.c_str(), service->type.c_str(),
    channel_name(service_channel(info.name, "request")).c_str());
}

void ServiceProxyService::serviceLoop(const std::shared_ptr<ServiceProxy> & service)
{
  const ServiceCodec & codec = *service->codec;
  std::vector<std::uint8_t> buffer(service->requests->slot_size());
  const bool (*running)() = nullptr;
  (void)running;
  while ((!context_.running || context_.running()) && service->running) {
    std::size_t size = 0;
    if (!service->requests->subscribe(buffer.data(), buffer.size(), &size, 100)) {
      continue;
    }
    if (size < sizeof(std::uint64_t)) {
      continue;
    }
    std::uint64_t correlation = 0;
    std::memcpy(&correlation, buffer.data(), sizeof(correlation));
    const ros2_grpc_sdk::Bytes shim(
      buffer.begin() + sizeof(correlation), buffer.begin() + size);

    ros2_grpc_sdk::Bytes request_cdr;
    if (!codec.request_to_cdr(shim, request_cdr)) {
      service->failures.fetch_add(1);
      std::fprintf(
        stderr, "[bridge] '%s': undecodable request from shared memory\n",
        service->name.c_str());
      continue;
    }

    ros2_grpc_sdk::ServiceCallResult result;
    const ros2_grpc_sdk::Status status = context_.client->callService(
      service->name, service->type, request_cdr, result, "cdr");
    if (!status.ok()) {
      service->failures.fetch_add(1);
      std::fprintf(
        stderr, "[bridge] '%s': call failed: %s\n", service->name.c_str(),
        status.toString().c_str());
      continue;
    }
    if (!result.success) {
      service->failures.fetch_add(1);
      std::fprintf(
        stderr, "[bridge] '%s': %s\n", service->name.c_str(), result.error.c_str());
      continue;
    }

    ros2_grpc_sdk::Bytes response_shim;
    if (!codec.response_to_shim(result.response, response_shim)) {
      service->failures.fetch_add(1);
      std::fprintf(
        stderr, "[bridge] '%s': undecodable response for %s\n", service->name.c_str(),
        service->type.c_str());
      continue;
    }

    ros2_grpc_sdk::Bytes out(sizeof(correlation) + response_shim.size());
    std::memcpy(out.data(), &correlation, sizeof(correlation));
    std::memcpy(out.data() + sizeof(correlation), response_shim.data(), response_shim.size());
    if (!service->responses->publish(out.data(), out.size())) {
      service->failures.fetch_add(1);
      std::fprintf(
        stderr, "[bridge] '%s': the answer does not fit the response slot (%zu bytes)\n",
        service->name.c_str(), out.size());
      continue;
    }
    service->calls.fetch_add(1);
    service->last_elapsed_ns.store(result.elapsed_ns);
  }
}

std::size_t ServiceProxyService::service_count() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return services_.size();
}

void ServiceProxyService::append_report(std::vector<std::string> & lines) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto & entry : services_) {
    const auto & service = entry.second;
    char buffer[256];
    std::snprintf(
      buffer, sizeof(buffer), "  s %-26s %-34s calls=%-8llu failed=%llu",
      service->name.c_str(), service->type.c_str(),
      static_cast<unsigned long long>(service->calls.load()),
      static_cast<unsigned long long>(service->failures.load()));
    lines.push_back(buffer);
  }
}

void ServiceProxyService::stop()
{
  std::vector<std::shared_ptr<ServiceProxy>> services;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto & entry : services_) {
      entry.second->running = false;
      services.push_back(entry.second);
    }
  }
  const std::string & peer = context_.peer != nullptr ? *context_.peer : std::string();
  // Stopped before the client is torn down: a worker blocked in a call has to
  // be out of the way before the channel goes.
  for (const auto & service : services) {
    if (service->worker.joinable()) {
      service->worker.join();
    }
    ros2_shim::graph::ProcessRegistry::instance().remove_service(
      service->name, "remote:" + peer);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  services_.clear();
}

}  // namespace ros2_bridge
