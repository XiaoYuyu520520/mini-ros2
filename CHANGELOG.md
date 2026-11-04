# Changelog

All notable changes to this project. The format is chronological; the newest
release is first. Versions before v0.2.4 predate this file — their notes live on
the GitHub Releases page.

## Unreleased

### Added

* **The bridge's registry tables are generated.** `bridge/tests/registry_manifest.txt`
  is now the only place a topic message, a service or an action is named. A
  generator resolves every entry against the shim's *generated* headers - the
  same headers a nav2 node compiles against, so a type that does not exist on
  this side cannot be registered by accident - and writes the include list and
  the three tables. `--check` fails when the checked-in files no longer match
  the manifest, and it runs in both cross-platform jobs and in the release
  workflow. The pretty printer and the field setter stay hand-written: which
  fields of a `LaserScan` are worth printing is a decision, not a list.
* **The graph registry caches parsed files** (`graph.hpp`). Reading the graph
  meant opening and parsing every `<pid>.graph` on the machine, and a graph
  rarely changes between two refreshes. Measured with ten files and 207
  topics: 1.494 ms -> 0.299 ms per read, 5x.
* **The relay keeps its percentiles ready** instead of sorting on demand, so a
  snapshot no longer holds its lock through a hundred sorts: 0.549 ms -> 0.191
  ms at a hundred topics.
* **The shared memory's checksum is hardware-accelerated**: CRC32C with the
  SSE4.2 / ARM CRC instruction, chosen at runtime, with a slice-by-8 fallback.
* Two benchmarks, because the numbers decided the work:
  `nav2/shm_transport/tests/shm_fastpath_bench.cpp` (what a publish costs, split
  into the copy, the checksum and the rest) and
  `nav2/ros2_shim/tests/graph_cache_bench.cpp` (what the graph cache saves).

### Fixed

* **The shared memory slot's checksum was never checked.** The field was
  written on every publish and the reader never compared it - 94% of a laser
  scan's publish time for a value nobody read. The reader now checks it, and a
  slot that does not match is counted as dropped instead of handed over as if
  it were whole.
* **A held graph file no longer reads as an unreadable one.** The reader waits
  a second for a writer's swap and then gave up, which under an oversubscribed
  machine reported a running process as invisible (`04-gaps.md`, section 7).
  It now falls back to the copy from before the swap - one version old at
  worst - and only for a file that is *held*; a removed record still
  disappears.
* The arm64 build of the CRC32C path needs `<arm_acle.h>`: `__crc32cd` and
  `__crc32cb` are ACLE intrinsics, not GCC builtins.

### Changed

* **The bridge daemon is five modules instead of one 1984-line class**:
  `daemon_options` (the command line and the log), `topic_mirror` (remote to
  local), `service_proxy` and `action_proxy` (the robot's services and actions,
  answered here), `uplink_forwarder` (this machine's topics on their way out).
  The daemon keeps what crosses them: the one gRPC connection, the peer, the
  options, the ready file and the report. Each module has its own lock for the
  state it owns, and they sit in the include tree rather than an anonymous
  namespace so a test can link them.

## v0.2.5 — 2026-11-03

### Fixed

* **ARM64 builds and ships.** The arm64 job had been red since the SDK was
  added. The cause was not the gRPC version: `ros2_bridge_client.hpp` declared
  `namespace grpc { class Channel; }`, while gRPC 1.30 defines `grpc::Channel`
  as `typedef grpc_impl::Channel` - a class declaration cannot agree with a
  typedef, and the headers then failed with "conflicting declaration". The
  declaration was unnecessary (the `.cpp` includes `<grpcpp/grpcpp.h>` before
  using it), so it was removed. Windows, Linux x86_64 and Linux ARM64 now all
  build, test and package from the same tree, and the `continue-on-error` on
  arm64 is gone.
* `mini-ros2-ubuntu22-arm64-<version>.tar.gz` is part of every release. The
  label says `ubuntu22` because the archive is built there and needs glibc
  2.35 (see `PLATFORMS` in `package.py`).

### Notes

* The archives are otherwise identical to v0.2.4; this release exists so the
  ARM64 package and the green arm64 gate are published.

## v0.2.4 — 2026-11-03

This release ships the correctness work that landed after v0.2.3 plus the
follow-up fixes from the second review.

### Fixed — cross-machine relay

* **Colliding topics no longer mix data.** A topic both the simulator and the
  real robot publish (`/scan`, `/odom`, …) is split into `/scan` and
  `/real/scan`, and the relay now publishes each source under its own merged
  name. Previously the second name was registered in the graph but the bytes
  still went to the plain one.
* **Echo suppression is provenance based.** The old rule muted a whole topic
  for 0.5 s after an injection and could drop a legitimate sample. An injected
  message now carries `origin_id`; the agent tags the copy it hands back, and
  only that copy is dropped. A byte-hash fallback remains for agents that do
  not support provenance, and is only used when the agent has no `source_id`
  either.
* **Dead subscriptions are recreated.** Each stream has a
  CONNECTING/ACTIVE/RETRYING/FAILED state and is restarted with jittered
  exponential backoff. The failure counter is no longer reset just because an
  RPC object was created, so a stream that dies immediately keeps backing off
  (up to 30 s) instead of retrying every 0.5 s forever.
* **`Publish` reports what actually happened.** The relay returns
  `ACCEPTED` / `REJECTED` / `UNAVAILABLE` / `TIMEOUT`, and only reports
  `accepted=true` after the target agent accepted the message.
* **Control commands are never inferred onto the wrong robot.** `/cmd_vel`,
  `/cmd_vel_nav`, `/cmd_vel_raw` and `/cmd_vel_smoothed` require an explicit
  route, or exactly one consumer while both upstreams are reachable. An
  ambiguous or offline target is refused instead of falling back to the
  simulator. Optional Twist speed clamps are available.
* **The merged graph is a snapshot.** Topics, services and actions that an
  upstream no longer advertises are removed from `ListTopics` / `ListServices`
  / `ListActions` instead of lingering forever; a topic recreated with a new
  type is advertised with that type.
* **Action feedback failures surface.** A broken feedback stream now aborts
  with the upstream gRPC status instead of ending silently.
* **Provenance cleanup is amortised.** Expired echo/trace entries are swept at
  most once per second instead of on every message.
* **Counters exist.** Per-topic received/merged/dropped/bytes and global
  reconnect/publish counters are available via `Relay.stats_snapshot()` and
  logged once a minute. There is no RPC or GUI view yet.

### Fixed — SDK, config and docs

* CDR decoding reports failure for implausible sequence lengths instead of
  returning a half-filled message; `cdr_check` covers a malformed payload.
* The visualiser writes `config.json` with a temp file + fsync + rename, and
  preserves a config that fails to parse as `<name>.broken-N.json`.
* Opt-in TLS and mutual TLS on the C++ agent (`ServerOptions`, `--tls-*`), the
  discovery probe, and the Python relay (`--tls-*`).
* `GETTING_STARTED.md` no longer claims `PointCloud2` / `Image` are missing;
  `04-gaps.md` now separates historical conclusions from current state.

### Known limits

* **No ARM64 archive** — fixed in v0.2.5, which publishes
  `mini-ros2-ubuntu22-arm64-<version>.tar.gz` and turns the arm64 CI job into
  a required gate. v0.2.4 and older have no ARM64 package.
* **Transport security is opt-in and not end-to-end tested.** Plaintext is
  still the default. There is no per-operation authorization, connection
  allow-list, audit log or message-size limit yet. Keep the ports on a trusted
  LAN or VPN.
* **No 30-minute dual-source soak** was run for the collision isolation. The
  in-tree tests assert message-level provenance and both sources; the long run
  remains a field check.
* **Metrics have no RPC or UI.** They are logs and an in-process snapshot.
* **The visualiser has no navigation action loop yet.** "2D Goal Pose"
  publishes `/goal_pose`; nav2 is driven through its action interface from the
  CLI. Planning is shown because `/plan` is drawn, but there is no
  send/cancel/feedback panel and no per-node diagnostics panel.