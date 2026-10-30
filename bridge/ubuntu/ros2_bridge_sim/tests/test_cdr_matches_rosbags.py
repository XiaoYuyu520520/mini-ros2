#!/usr/bin/env python3
"""Byte-for-byte validation of the CDR codec against the ``rosbags`` library.

``rosbags`` parses the real ``.msg`` files that ship with this repository, so
it is an independent oracle for the wire format: it derives field order and
types from the ``.msg`` files while :mod:`ros2_bridge_sim.messages` derives
them from the generated C++ headers.

Runs under pytest, or standalone::

    python tests/test_cdr_matches_rosbags.py

It skips cleanly (with a clear message) when ``rosbags`` is not installed or
when the repository's ``.msg`` sources cannot be found.
"""

from __future__ import annotations

import glob
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from ros2_bridge_sim import messages as m  # noqa: E402

try:  # pragma: no cover - depends on the environment
    import pytest
except ImportError:  # pragma: no cover
    pytest = None


def skip(reason: str):
    """Skip under pytest, or report and exit when run standalone."""
    if pytest is not None:
        pytest.skip(reason)
    print("SKIP: %s" % reason)
    raise SystemExit(0)


# --------------------------------------------------------------------------
# Locating the .msg sources
# --------------------------------------------------------------------------

def repo_root() -> str | None:
    """Repository root (the directory holding ``nav2/``), or None."""
    candidates = []
    env = os.environ.get("ROS2_BRIDGE_REPO")
    if env:
        candidates.append(env)
    here = os.path.dirname(os.path.abspath(__file__))
    candidates.append(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(here)))))
    candidates.append(os.getcwd())
    for candidate in candidates:
        if os.path.isdir(os.path.join(candidate, "nav2")):
            return candidate
    return None


def msg_files(root: str) -> list[str]:
    patterns = [
        os.path.join(root, "nav2", "third_party", "**", "*.msg"),
        # the brief names av2_msgs; the checkout ships the package as nav2_msgs
        os.path.join(root, "nav2", "nav2_extracted", "navigation2-1.5.1", "av2_msgs", "**", "*.msg"),
        os.path.join(root, "nav2", "nav2_extracted", "navigation2-1.5.1", "nav2_msgs", "**", "*.msg"),
    ]
    found: list[str] = []
    for pattern in patterns:
        for path in glob.glob(pattern, recursive=True):
            if path not in found:
                found.append(path)
    return found


def typename_of(path: str) -> str | None:
    """``.../rcl_interfaces-rolling/builtin_interfaces/msg/Time.msg`` -> ``builtin_interfaces/msg/Time``."""
    parts = path.replace(os.sep, "/").split("/")
    kind = parts[-2]
    if kind not in ("msg", "srv", "action"):
        return None
    package = re.sub(r"-\w+$", "", parts[-3])  # drop a "-rolling"/"-1.5.1" suffix
    return "%s/%s/%s" % (package, kind, parts[-1][:-4])


_STORE = None


def typestore():
    """A rosbags typestore built from this repository's ``.msg`` files."""
    global _STORE
    if _STORE is not None:
        return _STORE
    try:
        from rosbags.typesys import Stores, get_typestore
        from rosbags.typesys.msg import get_types_from_msg
    except ImportError as exc:
        skip("rosbags is not installed (%s)" % exc)

    root = repo_root()
    if root is None:
        skip("could not locate the repository root (set ROS2_BRIDGE_REPO)")
    files = msg_files(root)
    if not files:
        skip("no .msg sources found under %s" % root)

    types: dict = {}
    for path in files:
        name = typename_of(path)
        if name is None:
            continue
        with open(path, encoding="utf-8") as handle:
            types.update(get_types_from_msg(handle.read(), name))

    store = get_typestore(Stores.EMPTY)
    store.register(types)
    _STORE = store
    return store


def require(type_name: str):
    """The rosbags message class for ``type_name``."""
    store = typestore()
    if type_name not in store.types:
        skip("rosbags typestore has no %s" % type_name)
    return store.types[type_name]


# --------------------------------------------------------------------------
# Samples: (type name, plain dict for our encoder, rosbags object factory)
# --------------------------------------------------------------------------

def _header(store, sec, nanosec, frame_id):
    return store.types["std_msgs/msg/Header"](
        stamp=store.types["builtin_interfaces/msg/Time"](sec=sec, nanosec=nanosec),
        frame_id=frame_id,
    )


def _time(sec, nanosec):
    return {"sec": sec, "nanosec": nanosec}


def _header_dict(sec, nanosec, frame_id):
    return {"stamp": _time(sec, nanosec), "frame_id": frame_id}


def _pose_dict(x, y, z, yaw):
    return {
        "position": {"x": x, "y": y, "z": z},
        "orientation": {"x": 0.0, "y": 0.0, "z": math.sin(yaw / 2), "w": math.cos(yaw / 2)},
    }


def sample_laser_scan(store):
    ranges = [float("inf"), 0.5123, 1.0, 3.25, 12.0, float("inf")]
    intensities = [0.0, 1.5, 2.5, 3.5, 4.5, 5.5]
    scan_dict = {
        "header": _header_dict(17, 250000000, "laser"),
        "angle_min": -math.pi,
        "angle_max": math.pi,
        "angle_increment": 0.017453292519943295,
        "time_increment": 0.0,
        "scan_time": 0.1,
        "range_min": 0.15,
        "range_max": 12.0,
        "ranges": ranges,
        "intensities": intensities,
    }

    def build():
        import numpy as np

        return store.types["sensor_msgs/msg/LaserScan"](
            header=_header(store, 17, 250000000, "laser"),
            angle_min=-math.pi,
            angle_max=math.pi,
            angle_increment=0.017453292519943295,
            time_increment=0.0,
            scan_time=0.1,
            range_min=0.15,
            range_max=12.0,
            ranges=np.array(ranges, dtype=np.float32),
            intensities=np.array(intensities, dtype=np.float32),
        )

    return m.LASER_SCAN, scan_dict, build


def sample_odometry(store):
    odom_dict = {
        "header": _header_dict(3, 500000000, "odom"),
        "child_frame_id": "base_link",
        "pose": {
            "pose": _pose_dict(1.25, -2.5, 0.0, 0.75),
            "covariance": [0.001 if i % 7 == 0 else 0.0 for i in range(36)],
        },
        "twist": {
            "twist": {
                "linear": {"x": 0.5, "y": 0.0, "z": 0.0},
                "angular": {"x": 0.0, "y": 0.0, "z": -0.25},
            },
            "covariance": [0.002 if i % 7 == 0 else 0.0 for i in range(36)],
        },
    }

    def build():
        import numpy as np

        return store.types["nav_msgs/msg/Odometry"](
            header=_header(store, 3, 500000000, "odom"),
            child_frame_id="base_link",
            pose=store.types["geometry_msgs/msg/PoseWithCovariance"](
                pose=store.types["geometry_msgs/msg/Pose"](
                    position=store.types["geometry_msgs/msg/Point"](x=1.25, y=-2.5, z=0.0),
                    orientation=store.types["geometry_msgs/msg/Quaternion"](
                        x=0.0, y=0.0, z=math.sin(0.375), w=math.cos(0.375)
                    ),
                ),
                covariance=np.array([0.001 if i % 7 == 0 else 0.0 for i in range(36)], dtype=np.float64),
            ),
            twist=store.types["geometry_msgs/msg/TwistWithCovariance"](
                twist=store.types["geometry_msgs/msg/Twist"](
                    linear=store.types["geometry_msgs/msg/Vector3"](x=0.5, y=0.0, z=0.0),
                    angular=store.types["geometry_msgs/msg/Vector3"](x=0.0, y=0.0, z=-0.25),
                ),
                covariance=np.array([0.002 if i % 7 == 0 else 0.0 for i in range(36)], dtype=np.float64),
            ),
        )

    return m.ODOMETRY, odom_dict, build


def sample_tf_message(store):
    frames = [
        ("odom", "base_link", 1.0, 2.0, 0.5),
        ("base_link", "laser", 0.1, 0.0, 0.0),
        ("map", "odom", -3.5, 4.25, math.pi),
    ]
    tf_dict = {
        "transforms": [
            {
                "header": _header_dict(index, 125000000, parent),
                "child_frame_id": child,
                "transform": {
                    "translation": {"x": x, "y": y, "z": 0.0},
                    "rotation": {"x": 0.0, "y": 0.0, "z": math.sin(yaw / 2), "w": math.cos(yaw / 2)},
                },
            }
            for index, (parent, child, x, y, yaw) in enumerate(frames)
        ]
    }

    def build():
        return store.types["tf2_msgs/msg/TFMessage"](
            transforms=[
                store.types["geometry_msgs/msg/TransformStamped"](
                    header=_header(store, index, 125000000, parent),
                    child_frame_id=child,
                    transform=store.types["geometry_msgs/msg/Transform"](
                        translation=store.types["geometry_msgs/msg/Vector3"](x=x, y=y, z=0.0),
                        rotation=store.types["geometry_msgs/msg/Quaternion"](
                            x=0.0, y=0.0, z=math.sin(yaw / 2), w=math.cos(yaw / 2)
                        ),
                    ),
                )
                for index, (parent, child, x, y, yaw) in enumerate(frames)
            ]
        )

    return m.TF_MESSAGE, tf_dict, build


def sample_occupancy_grid(store):
    data = [0, 0, 100, 100, -1, 0, 100, -1, 0]
    grid_dict = {
        "header": _header_dict(0, 0, "map"),
        "info": {
            "map_load_time": _time(0, 0),
            "resolution": 0.05,
            "width": 3,
            "height": 3,
            "origin": _pose_dict(-12.0, -12.0, 0.0, 0.0),
        },
        "data": data,
    }

    def build():
        import numpy as np

        return store.types["nav_msgs/msg/OccupancyGrid"](
            header=_header(store, 0, 0, "map"),
            info=store.types["nav_msgs/msg/MapMetaData"](
                map_load_time=store.types["builtin_interfaces/msg/Time"](sec=0, nanosec=0),
                resolution=0.05,
                width=3,
                height=3,
                origin=store.types["geometry_msgs/msg/Pose"](
                    position=store.types["geometry_msgs/msg/Point"](x=-12.0, y=-12.0, z=0.0),
                    orientation=store.types["geometry_msgs/msg/Quaternion"](x=0.0, y=0.0, z=0.0, w=1.0),
                ),
            ),
            data=np.array(data, dtype=np.int8),
        )

    return m.OCCUPANCY_GRID, grid_dict, build


def sample_twist(store):
    twist_dict = {
        "linear": {"x": 0.75, "y": 0.0, "z": 0.0},
        "angular": {"x": 0.0, "y": 0.0, "z": -1.5},
    }

    def build():
        return store.types["geometry_msgs/msg/Twist"](
            linear=store.types["geometry_msgs/msg/Vector3"](x=0.75, y=0.0, z=0.0),
            angular=store.types["geometry_msgs/msg/Vector3"](x=0.0, y=0.0, z=-1.5),
        )

    return m.TWIST, twist_dict, build


def sample_path(store):
    poses = [(0.0, 0.0, 0.0), (1.5, 0.25, 0.5), (-2.0, 3.0, -1.25)]
    path_dict = {
        "header": _header_dict(9, 750000000, "map"),
        "poses": [
            {"header": _header_dict(index, 250000000, "map"), "pose": _pose_dict(x, y, 0.0, yaw)}
            for index, (x, y, yaw) in enumerate(poses)
        ],
    }

    def build():
        return store.types["nav_msgs/msg/Path"](
            header=_header(store, 9, 750000000, "map"),
            poses=[
                store.types["geometry_msgs/msg/PoseStamped"](
                    header=_header(store, index, 250000000, "map"),
                    pose=store.types["geometry_msgs/msg/Pose"](
                        position=store.types["geometry_msgs/msg/Point"](x=x, y=y, z=0.0),
                        orientation=store.types["geometry_msgs/msg/Quaternion"](
                            x=0.0, y=0.0, z=math.sin(yaw / 2), w=math.cos(yaw / 2)
                        ),
                    ),
                )
                for index, (x, y, yaw) in enumerate(poses)
            ],
        )

    return m.PATH, path_dict, build


def sample_log(store):
    """`rcl_interfaces/msg/Log` - what the Windows shim's `/rosout` carries."""
    log_dict = {
        "stamp": {"sec": 1789594420, "nanosec": 967772000},
        "level": 20,
        "name": "rosout_probe",
        "msg": "probe line 1 of 24",
        "file": "repo/nav2/ros2_shim/tests/rosout_probe.cpp",
        "function": "main",
        "line": 54,
    }

    def build():
        return store.types["rcl_interfaces/msg/Log"](
            stamp=store.types["builtin_interfaces/msg/Time"](sec=1789594420, nanosec=967772000),
            level=20,
            name="rosout_probe",
            msg="probe line 1 of 24",
            file="repo/nav2/ros2_shim/tests/rosout_probe.cpp",
            function="main",
            line=54,
        )

    return m.RCL_LOG, log_dict, build


SAMPLES = (
    sample_laser_scan,
    sample_odometry,
    sample_tf_message,
    sample_occupancy_grid,
    sample_twist,
    sample_path,
    sample_log,
)


def collect_samples():
    store = typestore()
    return [factory(store) for factory in SAMPLES]


# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------

def test_encoder_matches_rosbags_byte_for_byte():
    store = typestore()
    failures = []
    for type_name, message_dict, build in collect_samples():
        expected = bytes(store.serialize_cdr(build(), type_name))
        actual = m.encode(message_dict, type_name)
        if actual != expected:
            failures.append((type_name, expected, actual))
    assert not failures, "\n".join(
        "%s\n  rosbags: %s\n  ours   : %s" % (name, expected.hex(), actual.hex())
        for name, expected, actual in failures
    )


def test_decoder_round_trips_rosbags_bytes():
    store = typestore()
    failures = []
    for type_name, _message_dict, build in collect_samples():
        raw = bytes(store.serialize_cdr(build(), type_name))
        try:
            decoded = m.decode(raw, type_name)
        except Exception as exc:  # noqa: BLE001 - report, do not explode
            failures.append("%s: decode raised %s: %s" % (type_name, type(exc).__name__, exc))
            continue
        reencoded = m.encode(decoded, type_name)
        if reencoded != raw:
            failures.append(
                "%s: re-encode differs\n  rosbags: %s\n  ours   : %s"
                % (type_name, raw.hex(), reencoded.hex())
            )
    assert not failures, "\n".join(failures)


def test_decoded_values_are_sane():
    """Spot check that decoding lands the right values in the right fields."""
    store = typestore()
    type_name, _dict, build = sample_laser_scan(store)
    raw = bytes(store.serialize_cdr(build(), type_name))
    scan = m.decode(raw, type_name)
    assert scan["header"]["frame_id"] == "laser"
    assert scan["header"]["stamp"]["sec"] == 17
    assert scan["header"]["stamp"]["nanosec"] == 250000000
    assert len(scan["ranges"]) == 6
    assert scan["ranges"][0] == float("inf")
    assert abs(scan["ranges"][1] - 0.5123) < 1e-6
    assert scan["ranges"][4] == 12.0
    assert abs(scan["range_min"] - 0.15) < 1e-6

    type_name, _dict, build = sample_occupancy_grid(store)
    raw = bytes(store.serialize_cdr(build(), type_name))
    grid = m.decode(raw, type_name)
    assert grid["info"]["width"] == 3 and grid["info"]["height"] == 3
    assert grid["data"] == [0, 0, 100, 100, -1, 0, 100, -1, 0]

    type_name, _dict, build = sample_path(store)
    raw = bytes(store.serialize_cdr(build(), type_name))
    path = m.decode(raw, type_name)
    assert len(path["poses"]) == 3
    assert abs(path["poses"][1]["pose"]["position"]["x"] - 1.5) < 1e-12

    type_name, _dict, build = sample_odometry(store)
    raw = bytes(store.serialize_cdr(build(), type_name))
    odom = m.decode(raw, type_name)
    assert odom["child_frame_id"] == "base_link"
    assert abs(odom["twist"]["twist"]["angular"]["z"] + 0.25) < 1e-12

    _type_name, _dict, build = sample_tf_message(store)
    raw = bytes(store.serialize_cdr(build(), m.TF_MESSAGE))
    tf = m.decode(raw, m.TF_MESSAGE)
    assert [t["child_frame_id"] for t in tf["transforms"]] == ["base_link", "laser", "odom"]

    type_name, _dict, build = sample_log(store)
    raw = bytes(store.serialize_cdr(build(), type_name))
    log = m.decode(raw, type_name)
    assert log["level"] == 20
    assert log["name"] == "rosout_probe"
    assert log["msg"] == "probe line 1 of 24"
    assert log["function"] == "main" and log["line"] == 54
    assert log["stamp"]["sec"] == 1789594420
    assert log["stamp"]["nanosec"] == 967772000


def main() -> int:
    typestore()
    print("rosbags %s / typestore built from the repository .msg files" % _rosbags_version())
    print("sample types: %s" % ", ".join(name for name, _d, _b in collect_samples()))
    failures = 0
    for name, function in sorted(globals().items()):
        if not name.startswith("test_") or not callable(function):
            continue
        try:
            function()
        except AssertionError as exc:
            failures += 1
            print("FAIL %s\n%s" % (name, exc))
        except SystemExit:
            raise
        else:
            print("PASS %s" % name)
    print("=" * 60)
    print("%d failed" % failures if failures else "all CDR cross-checks passed against rosbags")
    return 1 if failures else 0


def _rosbags_version() -> str:
    try:
        from importlib.metadata import version

        return version("rosbags")
    except Exception:  # noqa: BLE001
        return "unknown"


if __name__ == "__main__":
    raise SystemExit(main())