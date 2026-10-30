#include "ros2_grpc_sdk/common/config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "ros2_grpc_sdk/common/log.hpp"
#include "ros2_grpc_sdk/common/yaml.hpp"

namespace ros2_grpc_sdk {
namespace {

namespace fs = std::filesystem;

std::string envOrEmpty(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr ? std::string(value) : std::string();
}

std::string joinPath(const std::string& directory, const std::string& name) {
  if (directory.empty()) {
    return name;
  }
  return (fs::path(directory) / name).string();
}

std::string homeDir() {
#if defined(_WIN32)
  std::string profile = envOrEmpty("USERPROFILE");
  if (!profile.empty()) {
    return profile;
  }
  const std::string drive = envOrEmpty("HOMEDRIVE");
  const std::string path = envOrEmpty("HOMEPATH");
  if (!drive.empty() || !path.empty()) {
    return drive + path;
  }
#endif
  return envOrEmpty("HOME");
}

std::string quote(const std::string& text) {
  std::string out = "\"";
  for (char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  out.push_back('"');
  return out;
}

std::vector<std::uint16_t> readPorts(const yaml::Value& value,
                                     std::vector<std::uint16_t> fallback) {
  if (!value.isArray()) {
    return fallback;
  }
  std::vector<std::uint16_t> ports;
  for (std::size_t i = 0; i < value.size(); ++i) {
    const int port = value.at(i).asInt(-1);
    if (port > 0 && port <= 65535) {
      ports.push_back(static_cast<std::uint16_t>(port));
    }
  }
  return ports.empty() ? fallback : ports;
}

std::vector<std::string> readStrings(const yaml::Value& value,
                                     std::vector<std::string> fallback) {
  if (!value.isArray()) {
    return fallback;
  }
  std::vector<std::string> items;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value.at(i).isString()) {
      items.push_back(value.at(i).asString());
    }
  }
  return items;
}

std::string flowPorts(const std::vector<std::uint16_t>& ports) {
  std::string out = "[";
  for (std::size_t i = 0; i < ports.size(); ++i) {
    if (i > 0) {
      out += ", ";
    }
    out += std::to_string(static_cast<unsigned>(ports[i]));
  }
  out += "]";
  return out;
}

std::string flowStrings(const std::vector<std::string>& items) {
  std::string out = "[";
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i > 0) {
      out += ", ";
    }
    out += quote(items[i]);
  }
  out += "]";
  return out;
}

std::string boolText(bool value) { return value ? "true" : "false"; }

}  // namespace

std::string configDirectory() {
  const std::string override_dir = envOrEmpty("ROS2_GRPC_SDK_CONFIG_DIR");
  if (!override_dir.empty()) {
    return override_dir;
  }

  std::error_code ec;
  if (fs::is_directory("config", ec)) {
    return fs::absolute("config", ec).string();
  }

#if defined(_WIN32)
  const std::string appdata = envOrEmpty("APPDATA");
  if (!appdata.empty()) {
    return joinPath(appdata, "ros2_gRPC_sdk");
  }
  return joinPath(homeDir(), "ros2_gRPC_sdk");
#else
  const std::string xdg = envOrEmpty("XDG_CONFIG_HOME");
  if (!xdg.empty()) {
    return joinPath(xdg, "ros2_gRPC_sdk");
  }
  const std::string home = homeDir();
  return joinPath(home.empty() ? "." : joinPath(home, ".config"),
                  "ros2_gRPC_sdk");
#endif
}

std::string configFilePath(const std::string& name) {
  return joinPath(configDirectory(), name);
}

Status loadConfig(Config& out, const std::string& path) {
  std::string file = path;
  if (file.empty()) {
    file = envOrEmpty("ROS2_GRPC_SDK_CONFIG");
  }
  if (file.empty()) {
    file = configFilePath();
  }

  std::error_code ec;
  if (!fs::exists(file, ec)) {
    const std::string fallback = joinPath("config", "ros2_gRPC_sdk.yaml");
    if (fs::exists(fallback, ec)) {
      file = fallback;
    } else {
      ROS2_GRPC_SDK_DEBUG("config: no file at ", file,
                          ", using built-in defaults");
      out = Config();
      return Status::success();
    }
  }

  std::ifstream stream(file, std::ios::binary);
  if (!stream) {
    return Status::error(StatusCode::Internal,
                         "cannot open config file: " + file);
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();

  yaml::Value root;
  std::string error;
  if (!yaml::Value::parse(buffer.str(), root, &error)) {
    return Status::error(StatusCode::InvalidArgument,
                         "config parse error in " + file + ": " + error);
  }
  if (!root.isObject()) {
    return Status::error(StatusCode::InvalidArgument,
                         "config root must be a YAML mapping: " + file);
  }

  Config config;
  if (const yaml::Value* server = root.find("server");
      server != nullptr && server->isObject()) {
    if (server->has("address")) {
      config.server.address =
          (*server)["address"].asString(config.server.address);
    }
    if (server->has("num_threads")) {
      config.server.num_threads =
          (*server)["num_threads"].asInt(config.server.num_threads);
    }
  }
  if (const yaml::Value* client = root.find("client");
      client != nullptr && client->isObject()) {
    if (client->has("connect_timeout_ms")) {
      config.client.connect_timeout_ms =
          (*client)["connect_timeout_ms"].asInt(config.client.connect_timeout_ms);
    }
    if (client->has("rpc_timeout_ms")) {
      config.client.rpc_timeout_ms =
          (*client)["rpc_timeout_ms"].asInt(config.client.rpc_timeout_ms);
    }
    if (client->has("use_tls")) {
      config.client.use_tls =
          (*client)["use_tls"].asBool(config.client.use_tls);
    }
    if (client->has("tls_root_cert_pem")) {
      config.client.tls_root_cert_pem =
          (*client)["tls_root_cert_pem"].asString();
    }
    if (client->has("tls_target_name_override")) {
      config.client.tls_target_name_override =
          (*client)["tls_target_name_override"].asString();
    }
  }
  if (const yaml::Value* discovery = root.find("discovery");
      discovery != nullptr && discovery->isObject()) {
    DiscoveryConfig& d = config.discovery;
    if (discovery->has("ports")) {
      d.ports = readPorts((*discovery)["ports"], d.ports);
    }
    if (discovery->has("connect_timeout_ms")) {
      d.connect_timeout_ms =
          (*discovery)["connect_timeout_ms"].asInt(d.connect_timeout_ms);
    }
    if (discovery->has("concurrency")) {
      d.concurrency = (*discovery)["concurrency"].asInt(d.concurrency);
    }
    if (discovery->has("max_hosts_per_subnet")) {
      d.max_hosts_per_subnet = static_cast<std::size_t>(
          (*discovery)["max_hosts_per_subnet"].asInt(
              static_cast<int>(d.max_hosts_per_subnet)));
    }
    if (discovery->has("verify_with_ping")) {
      d.verify_with_ping =
          (*discovery)["verify_with_ping"].asBool(d.verify_with_ping);
    }
    if (discovery->has("ping_timeout_ms")) {
      d.ping_timeout_ms =
          (*discovery)["ping_timeout_ms"].asInt(d.ping_timeout_ms);
    }
    if (discovery->has("overall_timeout_ms")) {
      d.overall_timeout_ms =
          (*discovery)["overall_timeout_ms"].asInt(d.overall_timeout_ms);
    }
    if (discovery->has("include_self")) {
      d.include_self = (*discovery)["include_self"].asBool(d.include_self);
    }
    if (discovery->has("include_loopback")) {
      d.include_loopback =
          (*discovery)["include_loopback"].asBool(d.include_loopback);
    }
    if (discovery->has("subnets")) {
      d.subnets = readStrings((*discovery)["subnets"], d.subnets);
    }
    if (discovery->has("max_results")) {
      d.max_results = static_cast<std::size_t>(
          (*discovery)["max_results"].asInt(static_cast<int>(d.max_results)));
    }
  }

  out = config;
  ROS2_GRPC_SDK_INFO("config loaded from ", file);
  return Status::success();
}

Status saveConfig(const Config& config, const std::string& path) {
  std::string file = path;
  if (file.empty()) {
    file = envOrEmpty("ROS2_GRPC_SDK_CONFIG");
  }
  if (file.empty()) {
    file = configFilePath();
  }

  std::error_code ec;
  const fs::path file_path(file);
  if (file_path.has_parent_path()) {
    fs::create_directories(file_path.parent_path(), ec);
  }

  std::ostringstream out;
  out << "# ros2_gRPC_sdk configuration (YAML - comments are supported).\n"
      << "# Regenerated by saveConfig(); edit the values in place.\n"
      << "\n"
      << "server:\n"
      << "  address: " << quote(config.server.address)
      << "   # agent bind address (0.0.0.0 = reachable on the whole LAN)\n"
      << "  num_threads: " << config.server.num_threads
      << "              # 0 = gRPC default\n"
      << "\n"
      << "client:\n"
      << "  connect_timeout_ms: " << config.client.connect_timeout_ms << "\n"
      << "  rpc_timeout_ms: " << config.client.rpc_timeout_ms << "\n"
      << "  use_tls: " << boolText(config.client.use_tls) << "\n"
      << "  tls_root_cert_pem: " << quote(config.client.tls_root_cert_pem)
      << "\n"
      << "  tls_target_name_override: "
      << quote(config.client.tls_target_name_override) << "\n"
      << "\n"
      << "discovery:\n"
      << "  ports: " << flowPorts(config.discovery.ports)
      << "                    # TCP ports scanned on each host\n"
      << "  connect_timeout_ms: " << config.discovery.connect_timeout_ms
      << "      # per (host, port) TCP probe\n"
      << "  concurrency: " << config.discovery.concurrency
      << "              # parallel probes\n"
      << "  max_hosts_per_subnet: "
      << config.discovery.max_hosts_per_subnet << "\n"
      << "  verify_with_ping: " << boolText(config.discovery.verify_with_ping)
      << "      # confirm each hit with a gRPC Ping\n"
      << "  ping_timeout_ms: " << config.discovery.ping_timeout_ms << "\n"
      << "  overall_timeout_ms: " << config.discovery.overall_timeout_ms
      << "     # total discovery budget\n"
      << "  include_self: " << boolText(config.discovery.include_self)
      << "          # false = exclude this machine\n"
      << "  include_loopback: " << boolText(config.discovery.include_loopback)
      << "      # false = exclude 127.0.0.0/8\n"
      << "  subnets: " << flowStrings(config.discovery.subnets)
      << "                # empty = all local subnets\n"
      << "  max_results: " << config.discovery.max_results << "\n";

  std::ofstream stream(file, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return Status::error(StatusCode::Internal,
                         "cannot write config file: " + file);
  }
  stream << out.str();
  if (!stream.good()) {
    return Status::error(StatusCode::Internal,
                         "failed while writing config file: " + file);
  }
  ROS2_GRPC_SDK_INFO("config saved to ", file);
  return Status::success();
}

DiscoveryOptions toDiscoveryOptions(const DiscoveryConfig& config) {
  DiscoveryOptions options;
  options.ports = config.ports;
  options.connect_timeout_ms = config.connect_timeout_ms;
  options.concurrency = config.concurrency;
  options.max_hosts_per_subnet = config.max_hosts_per_subnet;
  options.verify_with_ping = config.verify_with_ping;
  options.ping_timeout_ms = config.ping_timeout_ms;
  options.overall_timeout_ms = config.overall_timeout_ms;
  options.include_self = config.include_self;
  options.include_loopback = config.include_loopback;
  options.subnets = config.subnets;
  options.max_results = config.max_results;
  return options;
}

ClientOptions toClientOptions(const ClientConfig& config) {
  ClientOptions options;
  options.connect_timeout_ms = config.connect_timeout_ms;
  options.rpc_timeout_ms = config.rpc_timeout_ms;
  options.use_tls = config.use_tls;
  options.tls_root_cert_pem = config.tls_root_cert_pem;
  options.tls_target_name_override = config.tls_target_name_override;
  return options;
}

ServerOptions toServerOptions(const ServerConfig& config) {
  ServerOptions options;
  options.address = config.address;
  options.num_threads = config.num_threads;
  return options;
}

}  // namespace ros2_grpc_sdk
