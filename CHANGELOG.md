# Changelog

All notable changes to this project. The format is chronological; the newest
release is first. Versions before v0.2.4 predate this file — their notes live on
the GitHub Releases page.

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

* **No ARM64 archive.** The Ubuntu 22.04 arm runner ships gRPC 1.30 whose
  headers conflict with the SDK's channel code (`grpc_impl::Channel`). The
  arm64 CI job runs and is red; fixing it is a separate change.
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