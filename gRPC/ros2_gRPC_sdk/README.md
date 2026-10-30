# ros2_gRPC_sdk

A cross-platform **C++17** SDK that bridges a ROS2 graph over **gRPC** so that a
Windows (or Linux) machine on the same LAN can

* list the ROS2 topics of a remote device,
* subscribe to those topics (streaming),
* publish messages onto those topics,

without installing ROS2 on the client machine.

The SDK is split into a **client** (pure gRPC, builds anywhere) and an
**agent/server** (runs next to the ROS2 graph). The agent has a pluggable
`IRos2Backend` so it can either talk to a real ROS2 graph (`RclcppBackend`) or
to an in-memory simulation (`MockBackend`) for development and CI on machines
without ROS2.

```
                                   gRPC (LAN, protobuf)
  +---------------------+            ListTopics / GetTopicInfo        +-----------------------+
  |  Windows / Linux    |  ---------------------------------------->  |  Agent (Linux/Windows) |
  |  Ros2BridgeClient   |            Subscribe (server streaming)     |  Ros2BridgeServer      |
  |                     |  <----------------------------------------  |    IRos2Backend        |
  |  your application   |            Publish                           |    RclcppBackend  --> ROS2
  +---------------------+  ---------------------------------------->  |    MockBackend    --> sim
                                                                       +-----------------------+
```

---

## Features

* Single header-ish client API: `connect`, `listTopics`, `getTopicInfo`,
  `subscribe` (background streaming + callbacks), `unsubscribe`, `publish`,
  `ping`.
* Server API with a backend abstraction; the gRPC layer never depends on ROS2.
* Type-agnostic transport: messages travel as CDR-serialized bytes plus a type
  name, so no message type has to be known at compile time.
* `MockBackend` simulates a ROS2 graph (topics, live synthetic publishers and
  publish loopback) and is what the localhost end-to-end test uses.
* Fast **LAN discovery + auto-connect**: enumerate every network interface,
  concurrently probe configurable ports, confirm each hit with a gRPC Ping and
  connect to the lowest-latency agent. Localhost / this machine are excluded
  (modelled after the `netscan` reference project).
* Windows (MSYS2/UCRT64 MinGW and MSVC) and Linux via CMake.
* Optional TLS and connect/RPC timeouts on the client.
* Build/demo helper scripts are **Python** (no PowerShell/cmd batch files).

## Repository layout

```
ros2_gRPC_sdk/
  CMakeLists.txt
  cmake/ros2_grpc_sdkConfig.cmake.in
  proto/ros2_bridge.proto                 # gRPC service + messages
  include/ros2_grpc_sdk/
    ros2_grpc_sdk.hpp                     # umbrella header
    common/  status.hpp types.hpp log.hpp cdr.hpp ros2_types.hpp
    client/  ros2_bridge_client.hpp
    server/  i_ros2_backend.hpp mock_backend.hpp rclcpp_backend.hpp ros2_bridge_server.hpp
    discovery/ discovery.hpp              # LAN discovery / auto-connect
  src/
    common/  cdr.cpp ros2_types.cpp status.cpp log.cpp version.cpp
    client/  ros2_bridge_client.cpp
    server/  ros2_bridge_server.cpp mock_backend.cpp rclcpp_backend.cpp
    discovery/ discovery.cpp
  examples/  mock_agent_main.cpp list_topics_main.cpp subscribe_main.cpp
             publish_main.cpp bridge_demo_main.cpp discover_main.cpp
  tests/     localhost_e2e.cpp services_localhost.cpp discovery_localhost.cpp
             config_localhost.cpp actions_rclcpp.cpp   # the last one needs ROS2
  config/    ros2_gRPC_sdk.yaml                # default configuration (YAML)
  scripts/   platform_utils.py config.py build.py run_local_demo.py discover.py
```

Builds are written per platform into `build/<platform-key>/` (for example
`build/windows-x86_64/`, `build/linux-aarch64/`) and indexed in
`build/platforms.yaml`.

## The gRPC service

`proto/ros2_bridge.proto` defines `ros2bridge.Ros2Bridge`:

| RPC | Kind | Purpose |
| --- | --- | --- |
| `Ping` | unary | liveness/version handshake |
| `ListTopics` | unary | list topics, optional regex `name_filter` |
| `GetTopicInfo` | unary | resolve one topic's type and endpoint counts |
| `Subscribe` | server streaming | push every message of a topic until cancelled |
| `Publish` | unary | publish one serialized message |
| `ListServices` | unary | list services, optional regex `name_filter` |
| `GetServiceInfo` | unary | resolve one service's type |
| `CallService` | unary | one request/response round trip against a service |
| `ListActions` | unary | list action servers, optional regex `name_filter` |
| `GetActionInfo` | unary | resolve one action's type |
| `SendActionGoal` | unary | hand a goal to an action server: accepted or rejected |
| `ActionFeedback` | server streaming | feedback while the goal runs, then one terminal result |
| `GetActionResult` | unary | poll the result of a goal started with `SendActionGoal` |
| `CancelActionGoal` | unary | cancel a goal that is still running |

Messages carry `bytes data` + `encoding`. Real ROS2 uses `encoding = "cdr"`
(a `rclcpp::SerializedMessage`, including its 4-byte encapsulation header).
Helpers for `std_msgs/msg/String` and `Int64` live in
`ros2_grpc_sdk/common/ros2_types.hpp`.

Service and action payloads are opaque in exactly the same way: the SDK transports
`bytes` for a request, a response, a goal, feedback and a result, and the schemas
belong to the two ends. `SendActionGoal` answers as soon as the server has taken
the goal (or refused it), `ActionFeedback` carries the feedback stream and ends
with one terminal message holding the result, and `GetActionResult` /
`CancelActionGoal` are there for a caller that came back later. A goal is
identified by the id the server assigned in `SendActionGoalResponse`.

The Windows side of this repository is the heaviest consumer of all nine RPCs:
the bridge daemon mirrors a remote graph, so `ros2.bat service call` /
`ros2.bat action send_goal` end up on `CallService`, `SendActionGoal`,
`ActionFeedback` and `GetActionResult`. Cross-machine runs are recorded in
`../../bridge/docs/03-validation.md` (证据 17).

---

## Building on Windows (MSYS2 UCRT64)

All helper scripts are Python and invoke cmake/compilers directly. Use the
Python interpreter of your choice (e.g. the project venv):

```powershell
# 1. Install dependencies once (gRPC, protobuf, abseil, re2 ...)
& "D:\WindowsServer2025_1\software\MSYS2\usr\bin\pacman.exe" -S --noconfirm --needed `
    mingw-w64-ucrt-x86_64-grpc mingw-w64-ucrt-x86_64-protobuf

# 2. Configure + build
& "E:\venvs\py312\Scripts\python.exe" scripts/build.py

# ... with the test run afterwards
& "E:\venvs\py312\Scripts\python.exe" scripts/build.py --test
```

`scripts/build.py` locates the MSYS2 UCRT64 toolchain (`--msys-root`, or
`CPU_MSYS2_ROOT`, to override), exports it on `PATH`, then configures with Ninja
and builds. It imports the toolchain and core policy from the workspace's
`../../tools/_toolchain.py` and `../../tools/_cores.py`, so it defaults to the
same `-j 6` on the six P-cores as every other build entry point in this repo.
The underlying cmake invocation it performs is equivalent to:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER="D:/WindowsServer2025_1/software/MSYS2/ucrt64/bin/g++.exe" `
  -DCMAKE_PREFIX_PATH="D:/WindowsServer2025_1/software/MSYS2/ucrt64"
cmake --build build
```

> The UCRT64 `bin` directory must be on `PATH` so the gRPC/protobuf DLLs are
> found at run time (the Python script takes care of this for the build).

MSVC + vcpkg works too: configure with
`-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` after
`vcpkg install grpc protobuf`.

`build.py` options: `--platform`, `--build-root`, `--clean`,
`--config Debug|Release|RelWithDebInfo`, `--with-ros2`, `--no-examples`,
`--no-tests`, `--test`, `--list`, `--msys-root`, `--vcpkg-root`,
`--vs-generator`, `--toolchain`.

Each platform builds into its own tree, `build/<platform-key>`, and that tree records the
configure it was built with (`.configure-fingerprint.json`). A run whose command line disagrees
with the record removes the tree and configures it again rather than reusing a cache that belongs
to a different build - `--clean` is the explicit form of the same thing. `--with-ros2` is the case
that made this necessary: it is written into the cache both ways (`=ON` / `=OFF`) on every
configure, because a cached `ON` used to survive later runs that did not pass the flag and fail
them at `find_package(rclcpp)` in a shell that never sourced ROS.

## Multi-platform builds and auto-detection

`build.py` inspects the host and builds every *available* toolchain, each into
its own directory, and records the result in `build/platforms.yaml`. The
launcher then reads that manifest and picks the binaries for the machine it is
running on - so the same command works on Windows and on Linux.

```bash
python scripts/build.py --list        # list detected platforms on this host
python scripts/build.py               # build the host platform (auto)
python scripts/build.py --platform all
python scripts/build.py --platform windows-x86_64,windows-x86_64-msvc
```

Platform keys look like `windows-x86_64` (MinGW/UCRT64), `windows-x86_64-msvc`
(MSVC via vcpkg), `linux-x86_64`, `linux-aarch64`, `darwin-arm64`. On Windows
both the GNU and, when a vcpkg installation is found, the MSVC toolchain are
detected; on Linux/macOS the native toolchain is detected. A cross target can
be added explicitly with a CMake toolchain file:

```bash
python scripts/build.py --platform linux-aarch64 \
    --toolchain cmake/linux-aarch64-toolchain.cmake
```

Resulting layout:

```
build/
  platforms.yaml            # what was built (os, arch, toolchain, exe suffix)
  windows-x86_64/           # one self-contained build tree per platform
  windows-x86_64-msvc/
  linux-aarch64/
```

Running the demo needs no platform argument - it detects the current host,
resolves the matching build directory from the manifest and launches the right
executables (`.exe` on Windows, no suffix elsewhere):

```bash
python scripts/run_local_demo.py
python scripts/run_local_demo.py --platform windows-x86_64
python scripts/run_local_demo.py --build-dir build/linux-x86_64
```

## Building on Linux

```bash
# Debian/Ubuntu: sudo apt install build-essential cmake ninja-build libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc
python3 scripts/build.py
```

Verified: configure, build and `--test` all pass on Ubuntu 24.04 with g++ 13.3
against the distribution's gRPC 1.51.1 / protobuf 3.21.12.

Debian and Ubuntu ship **no** `ProtobufConfig.cmake` with `libprotobuf-dev`
(only the gRPC config in the same directory, and even that one falls back to
plain `find_package(Protobuf)`), so `CMakeLists.txt` asks for protobuf in CONFIG
mode and falls back to CMake's own `FindProtobuf` module when there is none. On
Windows/MSYS2 the CONFIG route is the one taken, unchanged.

To build the real ROS2 backend (source `/opt/ros/<distro>/setup.bash` first):

```bash
python3 scripts/build.py --with-ros2
```

## CMake options

| Option | Default | Description |
| --- | --- | --- |
| `ROS2_GRPC_SDK_WITH_ROS2` | `OFF` | build `RclcppBackend` (needs a sourced ROS2 env) |
| `ROS2_GRPC_SDK_BUILD_EXAMPLES` | `ON` | build the example programs |
| `ROS2_GRPC_SDK_BUILD_TESTS` | `ON` | build the `ros2_grpc_sdk_*` ctest suites (plus
`actions_rclcpp` where `rclcpp_action` and `action_tutorials_interfaces` are installed) |
| `BUILD_SHARED_LIBS` | `OFF` | build the SDK as a shared library |

---

## Running the localhost test (no ROS2 required)

The `MockBackend` simulates a ROS2 graph so the whole data path can be
exercised on a single machine.

```powershell
# End-to-end test (starts an in-process agent, drives it through the client)
& "E:\venvs\py312\Scripts\python.exe" scripts/build.py --test

# Full guided demo (run the binary from its per-platform build dir)
./build/windows-x86_64/ros2_grpc_sdk_bridge_demo.exe 127.0.0.1:50055
```

Separate processes, exactly like the Windows <-> Linux setup - launched and
stopped by Python, which auto-detects the platform and build directory:

```powershell
& "E:\venvs\py312\Scripts\python.exe" scripts/run_local_demo.py
```

`run_local_demo.py` starts the agent, waits for the port, runs
`list_topics` / `publish` / `subscribe`, then stops the agent (agent output is
written to `<build-dir>/agent_demo.log`). To drive it by hand instead, launch
the binaries from the matching `build/<platform-key>/` directory:

```powershell
# terminal 1: the agent ("Linux" side)
./build/windows-x86_64/ros2_grpc_sdk_mock_agent.exe 0.0.0.0:50051

# terminal 2: the client ("Windows" side)
./build/windows-x86_64/ros2_grpc_sdk_list_topics.exe   localhost:50051
./build/windows-x86_64/ros2_grpc_sdk_subscribe.exe     localhost:50051 /demo/chatter std_msgs/msg/String 10
./build/windows-x86_64/ros2_grpc_sdk_publish.exe       localhost:50051 /demo/echo std_msgs/msg/String "hi"
```

## Using the SDK from your own CMake project

```cmake
find_package(ros2_grpc_sdk CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE ros2_grpc_sdk::ros2_grpc_sdk)
```

or drop the folder in as a subdirectory:

```cmake
add_subdirectory(third_party/ros2_gRPC_sdk)
target_link_libraries(my_app PRIVATE ros2_grpc_sdk::ros2_grpc_sdk)
```

Client code (this is the Windows side talking to a Linux agent):

```cpp
#include <ros2_grpc_sdk/ros2_grpc_sdk.hpp>
using namespace ros2_grpc_sdk;

Ros2BridgeClient client;
if (!client.connect("192.168.1.42:50051").ok()) return 1;

std::vector<TopicInfo> topics;
client.listTopics(topics, "^/camera/");        // optional regex filter

client.subscribe("/chatter", "std_msgs/msg/String",
                 [](const Message& m) { std::cout << ros2::decodeString(m.data) << "\n"; });
client.publish("/chatter", "std_msgs/msg/String", ros2::encodeString("hello from windows"));
```

Agent code (this is the Linux side):

```cpp
#include <ros2_grpc_sdk/server/rclcpp_backend.hpp>
#include <ros2_grpc_sdk/server/ros2_bridge_server.hpp>
using namespace ros2_grpc_sdk;

auto backend = std::make_shared<RclcppBackend>();
Ros2BridgeServer server(backend);
server.start("0.0.0.0:50051");   // reachable from the whole LAN
server.wait();
```

---

## Configuration file (config folder)

Settings live in a **YAML** config file in a dedicated config folder, so the
file can carry comments (JSON cannot). The repository ships a commented
`config/ros2_gRPC_sdk.yaml`:

```yaml
# ros2_gRPC_sdk configuration (YAML - comments are supported)
server:
  address: "0.0.0.0:50051"   # agent bind address (0.0.0.0 = reachable on the LAN)
  num_threads: 0             # 0 = gRPC default worker count

client:
  connect_timeout_ms: 3000
  rpc_timeout_ms: 5000
  use_tls: false
  tls_root_cert_pem: ""
  tls_target_name_override: ""

discovery:
  ports: [50051]             # TCP ports scanned on every candidate host
  connect_timeout_ms: 150    # per (host, port) TCP probe
  concurrency: 256
  max_hosts_per_subnet: 1024
  verify_with_ping: true     # confirm each hit with a gRPC Ping
  ping_timeout_ms: 500
  overall_timeout_ms: 3000   # total discovery budget
  include_self: false        # false = never target this machine
  include_loopback: false    # false = exclude the whole 127.0.0.0/8
  subnets: []                # empty = scan every local subnet
  max_results: 32
```

Config folder resolution (same in C++ and Python):

1. `ROS2_GRPC_SDK_CONFIG_DIR` environment variable (a directory)
2. `./config` if it exists (the folder shipped here)
3. per-user: `%APPDATA%\ros2_gRPC_sdk` (Windows) or
   `$XDG_CONFIG_HOME/ros2_gRPC_sdk` / `~/.config/ros2_gRPC_sdk`

`ROS2_GRPC_SDK_CONFIG` can point at a specific file. A missing file is not an
error - the built-in defaults are used. Comments (`# ...`) and blank lines are
ignored by the loader.

The SDK ships a small dependency-free YAML parser (`common/yaml.hpp`,
`common/yaml.cpp`); it is not a full YAML 1.2 implementation - it covers the
configuration subset: block mappings/sequences, inline flow sequences
(`[a, b]`), quoted/plain scalars, booleans, null and numbers.

C++ (`ros2_grpc_sdk/common/config.hpp`):

```cpp
Config config;
loadConfig(config);                         // resolves the config folder
Ros2BridgeClient client;
DiscoveredAgent chosen;
client.connectToNearestAgentFromConfig({}, &chosen);   // uses config.discovery
```

`saveConfig(config)` writes a commented YAML document back (creating the
folder), and `toDiscoveryOptions` / `toClientOptions` / `toServerOptions` map
the file onto the runtime option structs. The `ros2_grpc_sdk_discover` example
accepts `--config FILE` and otherwise uses the config folder.

Python (`scripts/config.py`, requires PyYAML) reads the same file:

```bash
python scripts/config.py            # prints the resolved dir/file and contents
```

## LAN discovery and auto-connect

Dropping a hard-coded IP is not required: the client can find the nearest
agent on its own. The discovery module mirrors the `netscan` reference
implementation:

1. enumerate every up, non-loopback NIC and derive its IPv4 subnet (CIDR);
2. expand each subnet into host addresses (network/broadcast dropped);
3. concurrently TCP-probe the configured ports on every host (fast liveness);
4. confirm each hit is a real bridge with a gRPC `Ping` and time the RTT;
5. deduplicate, sort by ascending latency and connect to the nearest agent.

Ports, timeouts, concurrency and the self/loopback flags default to the
[config file](#configuration-file-config-folder).

**Local-machine handling (this host is never its own target):**

* the exclusion set is built from **all** adapters - up *and* down - so a
  temporarily disconnected NIC's address is still recognised as "this machine";
* the whole `127.0.0.0/8` range is dropped, not just `127.0.0.1`;
* enumeration includes every unicast IPv4 on every adapter (multi-homed hosts);
* results are deduplicated by `host:port`, so the same agent reachable through
  two NICs is reported once;
* `include_self` / `include_loopback` (config or `--self`) opt back in for
  local testing.

Verified on a real dual-NIC host: with the agent bound to `0.0.0.0:50051` a
default scan reports **0 agents** (the host's own `192.168.1.111` and
`192.168.137.1` are excluded); `--self` reports both, sorted by latency
(`192.168.1.111` 0.26 ms before `192.168.137.1` 0.86 ms).

```cpp
#include <ros2_grpc_sdk/ros2_grpc_sdk.hpp>
using namespace ros2_grpc_sdk;

DiscoveryOptions options;
options.ports = {50051, 50052};      // ports to probe (default: 50051)
options.connect_timeout_ms = 150;    // per TCP probe
options.overall_timeout_ms = 3000;   // total budget
options.verify_with_ping = true;     // confirm with gRPC Ping, use RTT as latency
// options.include_self = false;     // default: skip this host / loopback
// options.subnets = {"192.168.1.0/24"}; // default: every local subnet

Ros2BridgeDiscovery discovery(options);

std::vector<DiscoveredAgent> agents; // sorted by ascending latency
discovery.discover(agents);
for (const auto& a : agents) {
  std::cout << a.target() << "  " << a.latency_ms << " ms\n";
}

// One-call convenience: discover + connect the client to the nearest agent.
Ros2BridgeClient client;
DiscoveredAgent chosen;
client.connectToNearestAgent(options, &chosen);
client.listTopics(/* ... */);
```

`DiscoveryOptions`, `DiscoveredAgent`, `listNetworkInterfaces()`,
`localIPv4Addresses()` and `expandCidr()` are exported from
`ros2_grpc_sdk/discovery/discovery.hpp`. An agent listening on
`0.0.0.0:50051` (the `ServerOptions` default) is discoverable from the LAN.

Command line helper:

```bash
./build/windows-x86_64/ros2_grpc_sdk_discover.exe [ports] [timeout_ms] [subnet] [--self] [--config FILE]
# e.g. probe 50051/50052 on every NIC, 3s budget
./build/windows-x86_64/ros2_grpc_sdk_discover.exe 50051,50052 3000
# verify locally against loopback (normally excluded)
./build/windows-x86_64/ros2_grpc_sdk_discover.exe 50051 4000 127.0.0.1/32 --self

# or through the Python launcher (auto-detects the platform/build dir)
python scripts/discover.py --ports 50051,50052 --timeout 3000
python scripts/discover.py --self --subnet 127.0.0.1/32
```

The `discovery_localhost` and `config_localhost` ctests cover CIDR expansion,
discovery, lowest-latency selection, `connectToNearestAgent`, the self/loopback
exclusion (whole `127.0.0.0/29` range) and the config round-trip.

## Deploying against a real ROS2 graph

1. On the ROS2 machine, build with `python3 scripts/build.py --with-ros2` and
   run the agent (`RclcppBackend`), binding to `0.0.0.0:<port>` so it is
   reachable from the LAN.
2. Make sure the machine's firewall allows the chosen TCP port.
3. On the Windows client, `connect("<ros-host>:<port>")` and use the same API
   as in the localhost demo. No ROS2 installation is needed on Windows.

`RclcppBackend` lists topics via `Node::get_topic_names_and_types()`, publishes
through `rclcpp::GenericPublisher` and subscribes through
`rclcpp::GenericSubscription`, so it works with **any** message type whose
type support library is installed on the ROS2 machine.

> Note: `RclcppBackend` was written against the ROS2 Humble/Iron generic
> pub/sub API but could not be executed in this repo (no ROS2 machine
> available). Validate it on your Linux box before production use; the gRPC
> layer, `MockBackend` and the full client path are covered by the localhost
> end-to-end test.

## Encoding notes

* CDR payloads follow the DDS-CDR layout used by ROS2: a 4-byte encapsulation
  header (`00 01 00 00` little-endian plain CDR) followed by 4-byte aligned
  field data. Strings are length-prefixed (length includes the trailing NUL).
* `ros2_grpc_sdk::cdr::Writer` / `Reader` implement the primitives needed for
  the builtin types; `ros2::encodeString` / `decodeString` and
  `ros2::encodeInt64` / `decodeInt64` are ready-made helpers.
* User-defined message types can be carried as opaque CDR bytes. To convert to
  and from full message objects on the application side, use `rosidl` type
  support (or generate your own CDR accessors).

## Status

* Client, server, gRPC transport, mock backend, CDR helpers, LAN discovery,
  the config-folder loader, examples and tests: built and passing on
  Windows / MSYS2 UCRT64 **and on Ubuntu 24.04** (see `scripts/build.py --test`,
  4 ctest suites: `localhost_e2e`, `services_localhost`, `discovery_localhost`,
  `config_localhost`; a fifth, `actions_rclcpp`, joins them wherever
  `rclcpp_action` and `action_tutorials_interfaces` are installed, and drives
  the four `IRos2Backend` action RPCs through a real `rclcpp_action` server it
  starts itself). Discovery was exercised against the real dual-NIC host
  (self excluded by default, `--self` reports both NIC addresses sorted by
  latency).
* Linux was written down as supported long before it was ever configured there;
  the first run stopped at `find_package(Protobuf CONFIG REQUIRED)`. See
  `../../bridge/docs/04-gaps.md`, section 13.
* `RclcppBackend`: builds only with `ROS2_GRPC_SDK_WITH_ROS2=ON`; not executed
  here (no ROS2 machine available).
* Topic, service and action RPCs have all been driven across the real LAN by the
  bridge in `../../bridge/`: 7 mirrored topics, 5 services and 1 action reachable
  from Windows, `service call` and `action send_goal` round trips against the
  Ubuntu robot, feedback streaming at the simulator's rate, and a cancel that the
  far end reports as `CANCELED` with the distance it covered first
  (`../../bridge/docs/03-validation.md`, 证据 17).
