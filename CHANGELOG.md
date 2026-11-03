# Changelog

All notable changes to this project. The format is chronological; the newest
release is first. Versions before v0.2.4 predate this file — their notes live on
the GitHub Releases page.

## Unreleased

### Added

* **Latency and drop counters in the relay.** `ros2_link.relay` now measures the
  one-way delay of every message it merges - from the publishing agent's wall
  clock to its own - and reports P50/P95/P99/max per upstream and per topic.
  A timestamp that cannot be used (more than a minute old, or in the future
  because the two machines' clocks disagree) is counted as unusable instead of
  poisoning the percentiles. `stats_snapshot()` also carries `merge_errors`
  (the graph refused the message) and `echo_pending` (how many echo
  fingerprints are still queued).
* **Type-aware shared-memory slots.** An image or point cloud publisher now
  gets a channel sized for its payload (`NAV2_SHM_IMAGE_SLOT_SIZE`,
  `NAV2_SHM_POINTCLOUD_SLOT_SIZE`, 8 MB by default) instead of one sized by
  the topic's name. A 640x480 RGB8 frame is 921,600 bytes and
  `/camera/image_raw` says nothing about that.
* `Publisher::oversized_count()` - how many samples did not fit their slot.

### Fixed

* **A message larger than its slot was dropped in silence.** The publish
  failed, returned `false` and nothing said so, so a camera that "publishes"
  looked exactly like a topic nobody publishes. The first failure now prints
  the sample size, the slot size and the variables to raise.
* `ros2_grpc_sdk::cdr::Writer` had a private `reserve()` that resized the
  buffer instead of reserving it, and was never called. It is gone; the
  writer grew `writeRaw()`, `align()` and `little_endian()` instead.

### Changed

* Primitive arrays are encoded with one copy instead of one call per element:
  a laser scan's two float arrays and an odometry's two 36-element double
  arrays are the hot path, and the CDR encoder now aligns once and streams the
  array. The bytes are unchanged - `bridge_cdr_roundtrip` checks them against
  fixtures produced by `rosbags`, and the shim's own round trip keeps every
  value.
* `ros2_shim::serialize()` sizes its buffer before writing it, and
  `serializeInto()` reuses a caller's buffer, so a publisher that serializes
  every cycle allocates once instead of once per growth step.

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