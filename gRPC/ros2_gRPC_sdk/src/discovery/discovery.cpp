#include "ros2_grpc_sdk/discovery/discovery.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <grpcpp/grpcpp.h>

#include "ros2_grpc_sdk/common/log.hpp"
#include "ros2_bridge.grpc.pb.h"

namespace ros2_grpc_sdk {
namespace {

using Clock = std::chrono::steady_clock;

#if defined(_WIN32)
using socket_t = SOCKET;
const socket_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_t = int;
const socket_t kInvalidSocket = -1;
#endif

void ensureNetwork() {
#if defined(_WIN32)
  static std::once_flag flag;
  std::call_once(flag, []() {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
  });
#endif
}

void closeSocket(socket_t socket) {
#if defined(_WIN32)
  closesocket(socket);
#else
  ::close(socket);
#endif
}

void setNonBlocking(socket_t socket) {
#if defined(_WIN32)
  u_long mode = 1;
  ioctlsocket(socket, FIONBIO, &mode);
#else
  const int flags = fcntl(socket, F_GETFL, 0);
  fcntl(socket, F_SETFL, flags | O_NONBLOCK);
#endif
}

std::string formatIPv4(std::uint32_t host_order) {
  in_addr address;
  address.s_addr = htonl(host_order);
  char buffer[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, &address, buffer, sizeof(buffer));
  return buffer;
}

bool parseIPv4(const std::string& text, std::uint32_t& host_order) {
  in_addr address;
  if (inet_pton(AF_INET, text.c_str(), &address) != 1) {
    return false;
  }
  host_order = ntohl(address.s_addr);
  return true;
}

std::uint32_t prefixToMask(int prefix) {
  if (prefix <= 0) {
    return 0;
  }
  if (prefix >= 32) {
    return 0xFFFFFFFFu;
  }
  return 0xFFFFFFFFu << (32 - prefix);
}

int countPrefixBits(std::uint32_t mask) {
  int bits = 0;
  while (mask & 0x80000000u) {
    ++bits;
    mask <<= 1;
  }
  return bits;
}

std::string formatMac(const unsigned char* bytes, std::size_t length) {
  if (bytes == nullptr || length == 0) {
    return {};
  }
  static const char* digits = "0123456789ABCDEF";
  std::string mac;
  mac.reserve(length * 3);
  for (std::size_t i = 0; i < length; ++i) {
    if (i > 0) {
      mac.push_back(':');
    }
    mac.push_back(digits[(bytes[i] >> 4) & 0x0F]);
    mac.push_back(digits[bytes[i] & 0x0F]);
  }
  return mac;
}

#if defined(_WIN32)
std::string wideToUtf8(const wchar_t* text) {
  if (text == nullptr) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr,
                                       nullptr);
  if (size <= 1) {
    return {};
  }
  std::string out(static_cast<std::size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, &out[0], size, nullptr, nullptr);
  return out;
}
#endif

// Non-blocking TCP connect used as the fast liveness probe.
bool tcpProbe(const std::string& host, std::uint16_t port, int timeout_ms,
              double& latency_ms) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
    return false;
  }

  const socket_t socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket == kInvalidSocket) {
    return false;
  }
  setNonBlocking(socket);

  const auto start = Clock::now();
  bool connected = ::connect(socket, reinterpret_cast<sockaddr*>(&address),
                             sizeof(address)) == 0;
  if (!connected) {
    fd_set write_set;
    fd_set error_set;
    FD_ZERO(&write_set);
    FD_ZERO(&error_set);
    FD_SET(socket, &write_set);
    FD_SET(socket, &error_set);
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
#if defined(_WIN32)
    const int selected = ::select(0, nullptr, &write_set, &error_set, &timeout);
#else
    const int selected = ::select(socket + 1, nullptr, &write_set, &error_set,
                                  &timeout);
#endif
    if (selected > 0) {
      int error = 0;
#if defined(_WIN32)
      int error_length = sizeof(error);
#else
      socklen_t error_length = sizeof(error);
#endif
      if (getsockopt(socket, SOL_SOCKET, SO_ERROR,
                     reinterpret_cast<char*>(&error), &error_length) == 0) {
        connected = error == 0;
      }
    }
  }

  latency_ms = std::chrono::duration<double, std::milli>(Clock::now() - start)
                   .count();
  closeSocket(socket);
  return connected;
}

bool verifyAgent(const std::string& target, int timeout_ms,
                 DiscoveredAgent& agent) {
  grpc::ChannelArguments args;
  args.SetMaxReceiveMessageSize(4 * 1024 * 1024);
  auto channel =
      grpc::CreateCustomChannel(target, grpc::InsecureChannelCredentials(), args);
  if (!channel) {
    return false;
  }
  auto stub = ros2bridge::Ros2Bridge::NewStub(channel);

  const auto deadline =
      std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms);
  if (!channel->WaitForConnected(deadline)) {
    return false;
  }

  ros2bridge::PingRequest request;
  request.set_payload("discovery");
  ros2bridge::PingResponse response;
  grpc::ClientContext context;
  context.set_deadline(deadline);

  const auto start = Clock::now();
  const grpc::Status status = stub->Ping(&context, request, &response);
  const auto finish = Clock::now();
  if (!status.ok()) {
    return false;
  }

  agent.latency_ms =
      std::chrono::duration<double, std::milli>(finish - start).count();
  agent.ros_distro = response.ros_distro();
  agent.agent_version = response.agent_version();
  return true;
}

std::vector<NetworkInterface> enumerateInterfaces(bool include_loopback,
                                                  bool include_down) {
  ensureNetwork();
  std::vector<NetworkInterface> out;

#if defined(_WIN32)
  const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                      GAA_FLAG_SKIP_DNS_SERVER;
  ULONG size = 0;
  GetAdaptersAddresses(AF_INET, flags, nullptr, nullptr, &size);
  if (size == 0) {
    return out;
  }
  std::vector<char> buffer(size);
  auto* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
  if (GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &size) !=
      NO_ERROR) {
    return out;
  }

  for (auto* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
    const bool loopback = adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
    if (loopback && !include_loopback) {
      continue;
    }
    if (!include_down && adapter->OperStatus != IfOperStatusUp) {
      continue;
    }
    const std::string name = wideToUtf8(adapter->FriendlyName);
    const std::string mac = formatMac(adapter->PhysicalAddress,
                                      adapter->PhysicalAddressLength);
    for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr;
         unicast = unicast->Next) {
      if (unicast->Address.lpSockaddr == nullptr ||
          unicast->Address.lpSockaddr->sa_family != AF_INET) {
        continue;
      }
      auto* sin = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
      const std::uint32_t ip = ntohl(sin->sin_addr.s_addr);
      int prefix = static_cast<int>(unicast->OnLinkPrefixLength);
      if (prefix <= 0 || prefix > 32) {
        prefix = 24;
      }
      NetworkInterface info;
      info.name = name;
      info.ip = formatIPv4(ip);
      info.prefix_length = prefix;
      info.netmask = formatIPv4(prefixToMask(prefix));
      info.cidr = formatIPv4(ip & prefixToMask(prefix)) + "/" +
                  std::to_string(prefix);
      info.mac = mac;
      info.is_up = true;
      info.is_loopback = loopback;
      out.push_back(std::move(info));
    }
  }
#else
  ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces) != 0) {
    return out;
  }
  for (ifaddrs* current = interfaces; current != nullptr;
       current = current->ifa_next) {
    if (current->ifa_addr == nullptr ||
        current->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    const bool loopback = (current->ifa_flags & IFF_LOOPBACK) != 0;
    if (loopback && !include_loopback) {
      continue;
    }
    if (!include_down && (current->ifa_flags & IFF_UP) == 0) {
      continue;
    }
    auto* sin = reinterpret_cast<sockaddr_in*>(current->ifa_addr);
    const std::uint32_t ip = ntohl(sin->sin_addr.s_addr);
    std::uint32_t mask = 0xFFFFFF00u;
    if (current->ifa_netmask != nullptr) {
      mask = ntohl(reinterpret_cast<sockaddr_in*>(current->ifa_netmask)
                       ->sin_addr.s_addr);
    }
    const int prefix = countPrefixBits(mask);

    std::string mac;
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
      ifreq request{};
      std::strncpy(request.ifr_name, current->ifa_name, IFNAMSIZ - 1);
      if (ioctl(fd, SIOCGIFHWADDR, &request) == 0) {
        mac = formatMac(reinterpret_cast<unsigned char*>(
                            request.ifr_hwaddr.sa_data),
                        6);
      }
      ::close(fd);
    }

    NetworkInterface info;
    info.name = current->ifa_name;
    info.ip = formatIPv4(ip);
    info.prefix_length = prefix;
    info.netmask = formatIPv4(mask);
    info.cidr = formatIPv4(ip & mask) + "/" + std::to_string(prefix);
    info.mac = mac;
    info.is_up = true;
    info.is_loopback = loopback;
    out.push_back(std::move(info));
  }
  freeifaddrs(interfaces);
#endif

  return out;
}

}  // namespace

std::vector<NetworkInterface> listNetworkInterfaces(bool include_loopback,
                                                    bool include_down) {
  return enumerateInterfaces(include_loopback, include_down);
}

std::vector<std::string> localIPv4Addresses(bool include_loopback,
                                            bool include_down) {
  std::vector<std::string> addresses;
  for (const auto& info : enumerateInterfaces(include_loopback, include_down)) {
    addresses.push_back(info.ip);
  }
  return addresses;
}

bool isLoopbackAddress(const std::string& ip) {
  std::uint32_t value = 0;
  if (!parseIPv4(ip, value)) {
    return false;
  }
  return (value & 0xFF000000u) == 0x7F000000u;
}

std::vector<std::string> expandCidr(const std::string& cidr,
                                    std::size_t max_hosts,
                                    const std::vector<std::string>& exclude) {
  const auto slash = cidr.find('/');
  if (slash == std::string::npos) {
    return {};
  }
  std::uint32_t ip = 0;
  if (!parseIPv4(cidr.substr(0, slash), ip)) {
    return {};
  }
  const int prefix = std::atoi(cidr.substr(slash + 1).c_str());
  if (prefix < 0 || prefix > 32) {
    return {};
  }

  const std::uint32_t mask = prefixToMask(prefix);
  const std::uint32_t network = ip & mask;
  const std::uint32_t broadcast = network | ~mask;
  std::uint32_t start = network;
  std::uint32_t end = broadcast;
  if (prefix <= 30) {
    ++start;
    --end;
  }
  if (end < start) {
    return {};
  }

  const std::uint64_t total =
      static_cast<std::uint64_t>(end) - start + 1;
  if (total > max_hosts) {
    ROS2_GRPC_SDK_DEBUG("discovery: subnet ", cidr, " too large (", total,
                        " > ", max_hosts, "), skipped");
    return {};
  }

  const std::set<std::string> excluded(exclude.begin(), exclude.end());
  std::vector<std::string> hosts;
  hosts.reserve(static_cast<std::size_t>(total));
  for (std::uint32_t value = start;; ++value) {
    std::string text = formatIPv4(value);
    if (excluded.find(text) == excluded.end()) {
      hosts.push_back(std::move(text));
    }
    if (value == end) {
      break;
    }
  }
  return hosts;
}

Ros2BridgeDiscovery::Ros2BridgeDiscovery(DiscoveryOptions options)
    : options_(std::move(options)) {}

std::vector<NetworkInterface> Ros2BridgeDiscovery::interfaces() const {
  return listNetworkInterfaces(options_.include_loopback);
}

Status Ros2BridgeDiscovery::discover(std::vector<DiscoveredAgent>& agents_out,
                                     DiscoveryProgress progress) {
  ensureNetwork();
  agents_out.clear();

  const auto deadline =
      Clock::now() + std::chrono::milliseconds(std::max(1, options_.overall_timeout_ms));

  // 1. Figure out which subnets to scan.
  std::vector<NetworkInterface> nets;
  if (!options_.subnets.empty()) {
    for (const auto& cidr : options_.subnets) {
      NetworkInterface info;
      info.name = "(explicit)";
      info.cidr = cidr;
      info.is_up = true;
      nets.push_back(std::move(info));
    }
  } else {
    nets = listNetworkInterfaces(options_.include_loopback);
  }
  if (nets.empty()) {
    return Status::error(StatusCode::NotFound,
                         "no scannable network interfaces found");
  }

  // 2. Addresses to exclude: every address of this machine (all adapters,
  //    up or down, plus loopback). This is intentionally broader than the
  //    scannable set so a disconnected adapter's address is never probed.
  std::vector<std::string> exclude;
  if (!options_.include_self) {
    exclude = localIPv4Addresses(true, true);
  }

  struct Task {
    std::string host;
    std::uint16_t port;
    std::string interface_name;
  };
  std::vector<Task> tasks;
  for (const auto& net : nets) {
    if (net.cidr.empty()) {
      continue;
    }
    if (!options_.include_loopback && net.is_loopback) {
      continue;
    }
    const auto hosts = expandCidr(net.cidr, options_.max_hosts_per_subnet, exclude);
    for (const auto& host : hosts) {
      // The whole 127.0.0.0/8 range is this machine: skip it unless the
      // caller explicitly asks to include self/loopback.
      if (!options_.include_self && isLoopbackAddress(host)) {
        continue;
      }
      for (std::uint16_t port : options_.ports) {
        tasks.push_back(Task{host, port, net.name});
      }
    }
  }
  if (tasks.empty()) {
    return Status::error(StatusCode::NotFound, "no candidate hosts to probe");
  }

  ROS2_GRPC_SDK_INFO("discovery: probing ", tasks.size(),
                     " endpoint(s) on ", nets.size(), " subnet(s)");

  // 3. Fast concurrent TCP liveness sweep.
  std::atomic<std::size_t> next{0};
  std::atomic<std::size_t> probed{0};
  std::atomic<bool> stop{false};
  std::mutex hits_mutex;
  std::vector<DiscoveredAgent> hits;

  const std::size_t worker_count = static_cast<std::size_t>(
      std::max(1, std::min<int>(options_.concurrency,
                                static_cast<int>(tasks.size()))));
  auto worker = [&]() {
    while (!stop.load(std::memory_order_relaxed)) {
      const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
      if (index >= tasks.size()) {
        break;
      }
      if (Clock::now() > deadline) {
        stop.store(true, std::memory_order_relaxed);
        break;
      }
      const Task& task = tasks[index];
      double latency = 0.0;
      if (tcpProbe(task.host, task.port, options_.connect_timeout_ms, latency)) {
        DiscoveredAgent agent;
        agent.host = task.host;
        agent.port = task.port;
        agent.interface_name = task.interface_name;
        agent.latency_ms = latency;
        std::lock_guard<std::mutex> lock(hits_mutex);
        hits.push_back(std::move(agent));
      }
      const std::size_t done = probed.fetch_add(1) + 1;
      if (progress && (done % 16 == 0 || done == tasks.size())) {
        progress(done, tasks.size());
      }
    }
  };

  std::vector<std::thread> pool;
  pool.reserve(worker_count);
  for (std::size_t i = 0; i < worker_count; ++i) {
    pool.emplace_back(worker);
  }
  for (auto& thread : pool) {
    thread.join();
  }

  // 4. Deduplicate and (optionally) confirm with a gRPC Ping.
  std::set<std::string> seen;
  std::vector<DiscoveredAgent> unique;
  for (auto& hit : hits) {
    if (seen.insert(hit.target()).second) {
      unique.push_back(std::move(hit));
    }
  }

  std::vector<DiscoveredAgent> verified;
  if (options_.verify_with_ping) {
    for (auto& candidate : unique) {
      if (Clock::now() > deadline) {
        break;
      }
      DiscoveredAgent agent = candidate;
      if (verifyAgent(candidate.target(), options_.ping_timeout_ms, agent)) {
        verified.push_back(std::move(agent));
      } else {
        ROS2_GRPC_SDK_DEBUG("discovery: ", candidate.target(),
                            " is not a ros2 bridge");
      }
    }
  } else {
    verified = std::move(unique);
  }

  // 5. Lowest latency first.
  std::sort(verified.begin(), verified.end(),
            [](const DiscoveredAgent& lhs, const DiscoveredAgent& rhs) {
              if (lhs.latency_ms != rhs.latency_ms) {
                return lhs.latency_ms < rhs.latency_ms;
              }
              return lhs.target() < rhs.target();
            });
  if (verified.size() > options_.max_results) {
    verified.resize(options_.max_results);
  }

  ROS2_GRPC_SDK_INFO("discovery: ", verified.size(), " agent(s) found");
  agents_out = std::move(verified);
  return Status::success();
}

Status Ros2BridgeDiscovery::discoverBest(DiscoveredAgent& best_out,
                                         DiscoveryProgress progress) {
  std::vector<DiscoveredAgent> agents;
  const Status status = discover(agents, std::move(progress));
  if (!status.ok()) {
    return status;
  }
  if (agents.empty()) {
    return Status::error(StatusCode::NotFound, "no ros2 bridge agent found");
  }
  best_out = agents.front();
  return Status::success();
}

}  // namespace ros2_grpc_sdk
