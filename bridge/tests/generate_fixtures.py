#!/usr/bin/env python3
"""Generate CDR + shim fixtures used by `bridge_cdr_check`.

The point of this script is to produce reference bytes from an *independent*
implementation so the C++ transcoder in `bridge/windows/src/type_registry.cpp`
can be checked byte for byte:

* CDR comes from `rosbags` (ROS2 Jazzy type store), the same library used to
  read/write real ROS2 bags, so it is genuine on-the-wire CDR (CDR1, little
  endian, 4 byte encapsulation header, alignment relative to header end).
* the shim layout is re-implemented here directly from `ros2_shim`'s
  documented format (little endian, no padding, `string` = u32 length + bytes,
  `sequence<T>` = u32 count + elements, `array<T,N>` = N elements).

Outputs, under `bridge/tests/fixtures/`:

* `<name>.cdr`   raw CDR payload including the 4 byte encapsulation header
* `<name>.shim`  the equivalent shared-memory shim payload
* `manifest.tsv` `name<tab>type<tab>cdr_size<tab>shim_size`

Run with the workspace interpreter::

    python bridge/tests/generate_fixtures.py
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

import numpy as np
from rosbags.typesys import Stores, get_typestore, get_types_from_msg

FIXTURES = Path(__file__).resolve().parent / "fixtures"

PRIMITIVE = {
    "bool": "?",
    "byte": "b",
    "char": "B",
    "int8": "b",
    "uint8": "B",
    "int16": "h",
    "uint16": "H",
    "int32": "i",
    "uint32": "I",
    "int64": "q",
    "uint64": "Q",
    "float32": "f",
    "float64": "d",
}

ARRAY_RE = re.compile(r"^(?P<base>.+)\[(?P<count>\d+)\]$")


# --------------------------------------------------------------------------- shim layout

REPO_ROOT = Path(__file__).resolve().parents[2]
GENERATED_INCLUDE = REPO_ROOT / "nav2" / "ros2_shim" / "generated" / "include"

# `  ::geometry_msgs::msg::Pose pose;` / `  std::array<double, 36> covariance;`
# / `  double x = 0.0;` - the generated headers spell the message default out on
# the field, and a pattern that only accepted the bare declaration silently
# parsed those messages as empty (a `Twist` shim layout of 0 bytes instead of
# 48), which is what regenerating the fixtures used to do.
FIELD_RE = re.compile(
    r"^\s*(?P<type>(?:::)?[A-Za-z_]\w*(?:::\w+)*)(?:<(?P<args>.+)>)?\s+"
    r"(?P<name>[A-Za-z_]\w*)\s*(?:=[^;]*)?;\s*$")

# Not fields, even when they end in `;`: the alias/default lines a generated
# header carries next to the fields, some of which (`using x = y;`) would
# otherwise match FIELD_RE.
NOT_A_FIELD = ("using ", "static ", "typedef ", "friend ", "constexpr ", "template ",
               "return ", "#")
TYPE_RE = re.compile(r'ros2_message_type = "([^"]+)"')

CPP_SCALARS = {
    "bool": "?",
    "char": "B",
    "signed char": "b",
    "unsigned char": "B",
    "int8_t": "b",
    "uint8_t": "B",
    "int16_t": "h",
    "uint16_t": "H",
    "int32_t": "i",
    "uint32_t": "I",
    "int64_t": "q",
    "uint64_t": "Q",
    "float": "f",
    "double": "d",
}


def load_shim_structs() -> dict:
    """`pkg/msg/Name` -> ordered list of (cpp_type, template_args, field_name).

    The layout is read straight out of the generated shim headers, so the
    reference bytes below follow exactly what a nav2 node would see in shared
    memory - independently of the C++ transcoder being tested.
    """
    structs = {}
    for header in sorted(GENERATED_INCLUDE.rglob("*.hpp")):
        text = header.read_text(encoding="utf-8")
        if "ros2_message_tag" not in text:
            continue
        type_match = TYPE_RE.search(text)
        if not type_match:
            continue
        fields = []
        inside = False
        for line in text.splitlines():
            stripped = line.strip()
            if line.startswith("struct "):
                inside = True
                continue
            if not inside:
                continue
            # The generated header indents both, so this compares the stripped
            # line: `operator==` bodies hold lines (`return data == other.data;`)
            # that look like a field once default initialisers are accepted.
            if stripped.startswith(("template ", "bool operator", "friend ")):
                break
            if stripped.startswith(NOT_A_FIELD):
                continue
            match = FIELD_RE.match(line)
            if match:
                fields.append((match.group("type").lstrip(":"),
                               match.group("args") or "",
                               match.group("name")))
        structs[type_match.group(1)] = fields
    return structs


def _u32(out: bytearray, value: int) -> None:
    out += struct.pack("<I", value)


def _cpp_to_ros2(cpp_type: str) -> str:
    """`geometry_msgs::msg::Pose` -> `geometry_msgs/msg/Pose`."""
    parts = cpp_type.split("::")
    return f"{parts[0]}/{parts[1]}/{parts[2]}"


def encode_value(out: bytearray, kind: str, arg: str, value, structs) -> None:
    if kind == "scalar":
        out += struct.pack("<" + arg, value)
    elif kind == "bool":
        out += struct.pack("<B", 1 if value else 0)
    elif kind == "string":
        raw = value.encode("utf-8")
        _u32(out, len(raw))
        out += raw
    elif kind == "sequence":
        _u32(out, len(value))
        for element in value:
            encode_field(out, arg, element, structs)
    elif kind == "array":
        for element in value:
            encode_field(out, arg, element, structs)
    elif kind == "message":
        for cpp_type, args, name in structs[arg]:
            encode_field(out, join_cpp(cpp_type, args), getattr(value, name), structs)
    else:  # pragma: no cover - guarded by the parser
        raise AssertionError(f"unsupported shim field kind {kind}")


def join_cpp(cpp_type: str, args: str) -> str:
    return f"{cpp_type}<{args}>" if args else cpp_type


def encode_field(out: bytearray, cpp_type: str, value, structs) -> None:
    """Dispatch one field, mirroring `ros2_shim::WriteArchive`."""
    cpp_type = cpp_type.lstrip(":")
    if cpp_type in CPP_SCALARS:
        encode_value(out, "scalar", CPP_SCALARS[cpp_type], value, structs)
        return
    if cpp_type == "bool":
        encode_value(out, "bool", "", value, structs)
        return
    if cpp_type == "std::string":
        encode_value(out, "string", "", value, structs)
        return

    match = re.match(r"^(std::vector|std::array)<(.+)>$", cpp_type)
    if match:
        container, args = match.group(1), match.group(2)
        if container == "std::vector":
            encode_value(out, "sequence", args, value, structs)
        else:
            element, _, count = args.rpartition(",")
            element = element.strip()
            if len(value) != int(count.strip()):
                raise AssertionError(
                    f"expected {count.strip()} elements for {cpp_type}, got {len(value)}")
            encode_value(out, "array", element, value, structs)
        return

    if "::msg::" in cpp_type:
        encode_value(out, "message", _cpp_to_ros2(cpp_type), value, structs)
        return

    raise AssertionError(f"unsupported shim field type {cpp_type}")


def encode_message(structs, type_name: str, message) -> bytes:
    out = bytearray()
    encode_value(out, "message", type_name, message, structs)
    return bytes(out)


# --------------------------------------------------------------------------- messages



def register_local_message_types(typestore) -> int:
    """Teach the rosbags store the packages it does not ship.

    `Stores.ROS2_JAZZY` carries the common interfaces but not `nav2_msgs`,
    `dwb_msgs`, `map_msgs` or `nav_2d_msgs`, and most of nav2's own diagnostics
    live there. The definitions below are still `rosbags`' - only the message
    text comes from this workspace, which is the same text ROS2 generated the
    type support from.
    """
    definitions = {}
    for path in REPO_ROOT.rglob("*.msg"):
        parts = path.parts
        if "msg" not in parts or "build" in parts or "generated" in parts:
            continue
        index = len(parts) - 1 - parts[::-1].index("msg")
        if index < 2:
            continue
        name = f"{parts[index - 1]}/msg/{path.stem}"
        if name in typestore.types:
            continue
        definitions[name] = path.read_text(encoding="utf-8", errors="replace")

    parsed = {}
    for name, text in sorted(definitions.items()):
        try:
            parsed.update(get_types_from_msg(text, name))
        except Exception as error:  # pragma: no cover - unused packages
            print(f"  skipped {name}: {error}")
    typestore.register(parsed)
    return len(parsed)


def build_messages(typestore):
    """name -> (ros2 type, message).

    Every message is deliberately non-uniform (odd string lengths, mixed 4 and
    8 byte fields) so that alignment padding is exercised in both layouts.
    """
    t = typestore.types
    Header = t["std_msgs/msg/Header"]
    Time = t["builtin_interfaces/msg/Time"]

    def header(frame="map", sec=7, nsec=123456789):
        return Header(stamp=Time(sec=sec, nanosec=nsec), frame_id=frame)

    messages = {}

    messages["laserscan"] = (
        "sensor_msgs/msg/LaserScan",
        t["sensor_msgs/msg/LaserScan"](
            header=header("laser_frame"),
            angle_min=np.float32(-3.1415927),
            angle_max=np.float32(3.1415927),
            angle_increment=np.float32(0.008726646),
            time_increment=np.float32(0.000123),
            scan_time=np.float32(0.1),
            range_min=np.float32(0.12),
            range_max=np.float32(12.0),
            ranges=np.linspace(0.2, 11.5, 360, dtype=np.float32),
            intensities=np.array([1, 2, 3, 4, 5], dtype=np.float32),
        ),
    )

    # The camera pair. `Image` is the message whose payload is a byte array
    # rather than a handful of fields, so it is the one where a CDR/shim
    # disagreement about alignment is least visible: the same mistake in a
    # header is a wrong number, in `data` it is a wrong picture.
    messages["image"] = (
        "sensor_msgs/msg/Image",
        t["sensor_msgs/msg/Image"](
            header=header("camera_optical_frame", 9, 42),
            height=3,
            width=5,
            encoding="mono8",
            is_bigendian=0,
            step=5,
            data=np.arange(15, dtype=np.uint8),
        ),
    )

    messages["camera_info"] = (
        "sensor_msgs/msg/CameraInfo",
        t["sensor_msgs/msg/CameraInfo"](
            header=header("camera_optical_frame", 9, 43),
            height=480,
            width=640,
            distortion_model="plumb_bob",
            d=np.array([-0.0123, 0.0246, 0.0007, -0.0009, 0.0], dtype=np.float64),
            k=np.arange(9, dtype=np.float64) * 12.5,
            r=np.eye(3, dtype=np.float64).ravel(),
            p=np.arange(12, dtype=np.float64) * 3.5,
            binning_x=1,
            binning_y=2,
            roi=t["sensor_msgs/msg/RegionOfInterest"](
                x_offset=3, y_offset=4, height=100, width=200, do_rectify=True),
        ),
    )

    messages["odometry"] = (
        "nav_msgs/msg/Odometry",
        t["nav_msgs/msg/Odometry"](
            header=header("odom"),
            child_frame_id="base_link",
            pose=t["geometry_msgs/msg/PoseWithCovariance"](
                pose=t["geometry_msgs/msg/Pose"](
                    position=t["geometry_msgs/msg/Point"](x=1.25, y=-2.5, z=0.0),
                    orientation=t["geometry_msgs/msg/Quaternion"](
                        x=0.0, y=0.0, z=0.7071068, w=0.7071068),
                ),
                covariance=np.arange(36, dtype=np.float64) * 0.25,
            ),
            twist=t["geometry_msgs/msg/TwistWithCovariance"](
                twist=t["geometry_msgs/msg/Twist"](
                    linear=t["geometry_msgs/msg/Vector3"](x=0.5, y=0.0, z=0.0),
                    angular=t["geometry_msgs/msg/Vector3"](x=0.0, y=0.0, z=0.25),
                ),
                covariance=np.arange(36, dtype=np.float64) * -0.125,
            ),
        ),
    )

    messages["tfmessage"] = (
        "tf2_msgs/msg/TFMessage",
        t["tf2_msgs/msg/TFMessage"](
            transforms=[
                t["geometry_msgs/msg/TransformStamped"](
                    header=header("odom", 7, 1),
                    child_frame_id="base_link",
                    transform=t["geometry_msgs/msg/Transform"](
                        translation=t["geometry_msgs/msg/Vector3"](x=0.1, y=0.2, z=0.3),
                        rotation=t["geometry_msgs/msg/Quaternion"](
                            x=0.0, y=0.0, z=0.0, w=1.0),
                    ),
                ),
                t["geometry_msgs/msg/TransformStamped"](
                    header=header("base_link", 7, 2),
                    child_frame_id="laser_frame",
                    transform=t["geometry_msgs/msg/Transform"](
                        translation=t["geometry_msgs/msg/Vector3"](x=0.25, y=0.0, z=0.4),
                        rotation=t["geometry_msgs/msg/Quaternion"](
                            x=0.0, y=0.0, z=0.0, w=1.0),
                    ),
                ),
            ],
        ),
    )

    messages["twist"] = (
        "geometry_msgs/msg/Twist",
        t["geometry_msgs/msg/Twist"](
            linear=t["geometry_msgs/msg/Vector3"](x=0.3, y=-0.1, z=0.0),
            angular=t["geometry_msgs/msg/Vector3"](x=0.0, y=0.0, z=0.9),
        ),
    )

    messages["twist_stamped"] = (
        "geometry_msgs/msg/TwistStamped",
        t["geometry_msgs/msg/TwistStamped"](
            header=header("base_link"),
            twist=t["geometry_msgs/msg/Twist"](
                linear=t["geometry_msgs/msg/Vector3"](x=0.42, y=0.0, z=0.0),
                angular=t["geometry_msgs/msg/Vector3"](x=0.0, y=0.0, z=-0.33),
            ),
        ),
    )

    messages["path"] = (
        "nav_msgs/msg/Path",
        t["nav_msgs/msg/Path"](
            header=header("map"),
            poses=[
                t["geometry_msgs/msg/PoseStamped"](
                    header=header("map", 7, 1000 * index),
                    pose=t["geometry_msgs/msg/Pose"](
                        position=t["geometry_msgs/msg/Point"](
                            x=float(index) * 0.5, y=float(index) * 0.25, z=0.0),
                        orientation=t["geometry_msgs/msg/Quaternion"](
                            x=0.0, y=0.0, z=0.0, w=1.0),
                    ),
                )
                for index in range(3)
            ],
        ),
    )

    messages["occupancy_grid"] = (
        "nav_msgs/msg/OccupancyGrid",
        t["nav_msgs/msg/OccupancyGrid"](
            header=header("map"),
            info=t["nav_msgs/msg/MapMetaData"](
                map_load_time=Time(sec=7, nanosec=0),
                resolution=np.float32(0.05),
                width=np.uint32(8),
                height=np.uint32(5),
                origin=t["geometry_msgs/msg/Pose"](
                    position=t["geometry_msgs/msg/Point"](x=-5.0, y=-3.0, z=0.0),
                    orientation=t["geometry_msgs/msg/Quaternion"](
                        x=0.0, y=0.0, z=0.0, w=1.0),
                ),
            ),
            data=np.array([0, 0, 100, 100, -1, 0, 0, 0] * 5, dtype=np.int8),
        ),
    )

    messages["map_meta_data"] = (
        "nav_msgs/msg/MapMetaData",
        t["nav_msgs/msg/MapMetaData"](
            map_load_time=Time(sec=11, nanosec=7),
            resolution=np.float32(0.05),
            width=np.uint32(384),
            height=np.uint32(384),
            origin=t["geometry_msgs/msg/Pose"](
                position=t["geometry_msgs/msg/Point"](x=-9.6, y=-9.6, z=0.0),
                orientation=t["geometry_msgs/msg/Quaternion"](
                    x=0.0, y=0.0, z=0.0, w=1.0),
            ),
        ),
    )

    messages["string"] = (
        "std_msgs/msg/String",
        t["std_msgs/msg/String"](data="hello ros2 shim bridge"),
    )

    messages["bool"] = ("std_msgs/msg/Bool", t["std_msgs/msg/Bool"](data=True))

    messages["clock"] = (
        "rosgraph_msgs/msg/Clock",
        t["rosgraph_msgs/msg/Clock"](clock=Time(sec=1234567890, nanosec=987654321)),
    )

    messages["imu"] = (
        "sensor_msgs/msg/Imu",
        t["sensor_msgs/msg/Imu"](
            header=header("imu_link"),
            orientation=t["geometry_msgs/msg/Quaternion"](
                x=0.001, y=0.002, z=0.003, w=0.99999),
            orientation_covariance=np.eye(3, dtype=np.float64).reshape(-1) * 0.01,
            angular_velocity=t["geometry_msgs/msg/Vector3"](x=0.0, y=0.0, z=0.1),
            angular_velocity_covariance=np.eye(3, dtype=np.float64).reshape(-1) * 0.02,
            linear_acceleration=t["geometry_msgs/msg/Vector3"](x=0.0, y=0.0, z=9.81),
            linear_acceleration_covariance=np.eye(3, dtype=np.float64).reshape(-1) * 0.03,
        ),
    )

    messages["point_cloud2"] = (
        "sensor_msgs/msg/PointCloud2",
        t["sensor_msgs/msg/PointCloud2"](
            header=header("lidar"),
            height=np.uint32(1),
            width=np.uint32(4),
            fields=[
                t["sensor_msgs/msg/PointField"](
                    name="x", offset=np.uint32(0), datatype=np.uint8(7), count=np.uint32(1)),
                t["sensor_msgs/msg/PointField"](
                    name="y", offset=np.uint32(4), datatype=np.uint8(7), count=np.uint32(1)),
                t["sensor_msgs/msg/PointField"](
                    name="z", offset=np.uint32(8), datatype=np.uint8(7), count=np.uint32(1)),
            ],
            is_bigendian=False,
            point_step=np.uint32(12),
            row_step=np.uint32(48),
            data=np.arange(48, dtype=np.uint8),
            is_dense=True,
        ),
    )

    messages["pose_stamped"] = (
        "geometry_msgs/msg/PoseStamped",
        t["geometry_msgs/msg/PoseStamped"](
            header=header("map"),
            pose=t["geometry_msgs/msg/Pose"](
                position=t["geometry_msgs/msg/Point"](x=3.5, y=-1.25, z=0.0),
                orientation=t["geometry_msgs/msg/Quaternion"](
                    x=0.0, y=0.0, z=0.3826834, w=0.9238795),
            ),
        ),
    )

    messages["pose_with_covariance_stamped"] = (
        "geometry_msgs/msg/PoseWithCovarianceStamped",
        t["geometry_msgs/msg/PoseWithCovarianceStamped"](
            header=header("map"),
            pose=t["geometry_msgs/msg/PoseWithCovariance"](
                pose=t["geometry_msgs/msg/Pose"](
                    position=t["geometry_msgs/msg/Point"](x=1.0, y=2.0, z=0.0),
                    orientation=t["geometry_msgs/msg/Quaternion"](
                        x=0.0, y=0.0, z=0.0, w=1.0),
                ),
                covariance=np.arange(36, dtype=np.float64) * 0.5,
            ),
        ),
    )

    messages["pose_array"] = (
        "geometry_msgs/msg/PoseArray",
        t["geometry_msgs/msg/PoseArray"](
            header=header("map"),
            poses=[
                t["geometry_msgs/msg/Pose"](
                    position=t["geometry_msgs/msg/Point"](
                        x=float(index), y=float(-index), z=0.0),
                    orientation=t["geometry_msgs/msg/Quaternion"](
                        x=0.0, y=0.0, z=0.0, w=1.0),
                )
                for index in range(4)
            ],
        ),
    )

    messages["polygon_stamped"] = (
        "geometry_msgs/msg/PolygonStamped",
        t["geometry_msgs/msg/PolygonStamped"](
            header=header("map"),
            polygon=t["geometry_msgs/msg/Polygon"](
                points=[
                    t["geometry_msgs/msg/Point32"](x=0.5, y=0.5, z=0.0),
                    t["geometry_msgs/msg/Point32"](x=1.5, y=0.5, z=0.0),
                    t["geometry_msgs/msg/Point32"](x=1.5, y=1.5, z=0.0),
                ],
            ),
        ),
    )

    # visualization_msgs/msg/MarkerArray is intentionally not covered here: the
    # shim's generated Marker carries `UVCoordinate`/`MeshFile`, which the
    # rosbags Jazzy type store spells differently (`UvCoordinate`), so it cannot
    # be reconstructed from rosbags. The bridge registry still handles it.
    def point(x, y, z=0.0):
        return t["geometry_msgs/msg/Point"](x=float(x), y=float(y), z=float(z))

    def point32(x, y, z=0.0):
        return t["geometry_msgs/msg/Point32"](
            x=np.float32(x), y=np.float32(y), z=np.float32(z))

    def vector3(x, y, z):
        return t["geometry_msgs/msg/Vector3"](x=float(x), y=float(y), z=float(z))

    def pose(x, y, z=0.0):
        return t["geometry_msgs/msg/Pose"](
            position=point(x, y, z),
            orientation=t["geometry_msgs/msg/Quaternion"](x=0.0, y=0.0, z=0.0, w=1.0))

    def parameter(name, kind, value):
        # Lazily, and that matters: a dict here would evaluate every branch,
        # including `[str(item) for item in value]` on a scalar.
        if kind == 1:
            field, stored = "bool_value", bool(value)
        elif kind == 2:
            field, stored = "integer_value", int(value)
        elif kind == 3:
            field, stored = "double_value", float(value)
        elif kind == 4:
            field, stored = "string_value", str(value)
        elif kind == 5:
            field, stored = "byte_array_value", np.asarray(value, dtype=np.uint8)
        elif kind == 6:
            field, stored = "bool_array_value", np.asarray(value, dtype=np.bool_)
        elif kind == 7:
            field, stored = "integer_array_value", np.asarray(value, dtype=np.int64)
        elif kind == 8:
            field, stored = "double_array_value", np.asarray(value, dtype=np.float64)
        else:
            field, stored = "string_array_value", [str(item) for item in value]
        values = {
            "type": np.uint8(kind),
            "bool_value": False,
            "integer_value": 0,
            "double_value": 0.0,
            "string_value": "",
            "byte_array_value": np.zeros(0, dtype=np.uint8),
            "bool_array_value": np.zeros(0, dtype=np.bool_),
            "integer_array_value": np.zeros(0, dtype=np.int64),
            "double_array_value": np.zeros(0, dtype=np.float64),
            "string_array_value": [],
        }
        values[field] = stored
        return t["rcl_interfaces/msg/Parameter"](
            name=name, value=t["rcl_interfaces/msg/ParameterValue"](**values))

    def trajectory_score(index):
        return t["dwb_msgs/msg/TrajectoryScore"](
            traj=t["dwb_msgs/msg/Trajectory2D"](
                velocity=t["nav_2d_msgs/msg/Twist2D"](
                    x=np.float32(0.2), y=np.float32(0.0), theta=np.float32(0.1 * index)),
                time_offsets=[
                    t["builtin_interfaces/msg/Duration"](sec=0, nanosec=100000000 * (i + 1))
                    for i in range(2)
                ],
                poses=[pose(0.1 * i, 0.0) for i in range(3)],
            ),
            scores=[
                t["dwb_msgs/msg/CriticScore"](
                    name="PathAlign", raw_score=np.float32(-1.5), scale=np.float32(1.0)),
                t["dwb_msgs/msg/CriticScore"](
                    name="GoalDist", raw_score=np.float32(0.25), scale=np.float32(2.0)),
            ],
            total=np.float32(-1.5 - index),
        )

    messages["point_stamped"] = (
        "geometry_msgs/msg/PointStamped",
        t["geometry_msgs/msg/PointStamped"](
            header=header("map"), point=point(1.5, -2.25, 0.125)),
    )

    messages["polygon"] = (
        "geometry_msgs/msg/Polygon",
        t["geometry_msgs/msg/Polygon"](
            points=[
                point32(0.31, 0.31), point32(0.31, -0.31),
                point32(-0.31, -0.31), point32(-0.31, 0.31),
            ]),
    )

    # std_msgs/msg/Empty is deliberately not a fixture: `rosbags` (and rclpy)
    # generate it with a `structure_needs_at_least_one_member` byte, so their
    # CDR payload is 5 bytes where rclcpp's empty struct serialises to 4. Our
    # encoder writes what the C++ type holds - 4 - so an equality fixture here
    # would assert the wrong side of a rclpy/rclcpp difference. Decoding is
    # still covered: the registry takes both, and `bridge_cdr_check` decodes a
    # 5 byte payload if one is added back.

    messages["trajectory"] = (
        "nav_msgs/msg/Trajectory",
        t["nav_msgs/msg/Trajectory"](
            header=header("odom"),
            points=[
                t["nav_msgs/msg/TrajectoryPoint"](
                    header=header("odom", 7, 5 * index),
                    pose=pose(0.2 * index, 0.1 * index),
                    velocity=t["geometry_msgs/msg/Twist"](
                        linear=vector3(0.3, 0.0, 0.0), angular=vector3(0.0, 0.0, 0.2)),
                    acceleration=t["geometry_msgs/msg/Accel"](
                        linear=vector3(0.01, 0.0, 0.0), angular=vector3(0.0, 0.0, 0.0)),
                    effort=t["geometry_msgs/msg/Wrench"](
                        force=vector3(0.0, 0.0, 0.0), torque=vector3(0.0, 0.0, 0.0)),
                )
                for index in range(3)
            ]),
    )

    messages["occupancy_grid_update"] = (
        "map_msgs/msg/OccupancyGridUpdate",
        t["map_msgs/msg/OccupancyGridUpdate"](
            header=header("map"), x=np.int32(2), y=np.int32(3),
            width=np.uint32(4), height=np.uint32(3),
            data=np.array([0, 100, -1, 0] * 3, dtype=np.int8)),
    )

    messages["costmap_update"] = (
        "nav2_msgs/msg/CostmapUpdate",
        t["nav2_msgs/msg/CostmapUpdate"](
            header=header("odom"), x=np.uint32(1), y=np.uint32(2),
            size_x=np.uint32(3), size_y=np.uint32(2),
            data=np.array([0, 253, 254, 255, 1, 99], dtype=np.uint8)),
    )

    messages["costmap_filter_info"] = (
        "nav2_msgs/msg/CostmapFilterInfo",
        t["nav2_msgs/msg/CostmapFilterInfo"](
            header=header("map"), type=np.uint8(1),
            filter_mask_topic="/keepout_mask",
            base=np.float32(0.0), multiplier=np.float32(-1.0)),
    )

    messages["speed_limit"] = (
        "nav2_msgs/msg/SpeedLimit",
        t["nav2_msgs/msg/SpeedLimit"](
            header=header("odom"), percentage=True, speed_limit=0.35),
    )

    messages["voxel_grid"] = (
        "nav2_msgs/msg/VoxelGrid",
        t["nav2_msgs/msg/VoxelGrid"](
            header=header("odom"),
            data=np.array([0x00000000, 0xF0F0F0F0, 0x00000001], dtype=np.uint32),
            origin=point32(1.0, 2.0, 0.0),
            resolutions=vector3(0.05, 0.05, 0.2),
            size_x=np.uint32(16), size_y=np.uint32(16), size_z=np.uint32(4)),
    )

    messages["particle_cloud"] = (
        "nav2_msgs/msg/ParticleCloud",
        t["nav2_msgs/msg/ParticleCloud"](
            header=header("map"),
            particles=[
                t["nav2_msgs/msg/Particle"](pose=pose(0.1 * index, -0.25 * index),
                                            weight=0.125 * index)
                for index in range(3)
            ]),
    )

    messages["behavior_tree_log"] = (
        "nav2_msgs/msg/BehaviorTreeLog",
        t["nav2_msgs/msg/BehaviorTreeLog"](
            timestamp=Time(sec=8, nanosec=250),
            event_log=[
                t["nav2_msgs/msg/BehaviorTreeStatusChange"](
                    timestamp=Time(sec=8, nanosec=100 * index),
                    node_name="ComputePathToPose", uid=np.uint16(index),
                    previous_status="RUNNING", current_status="SUCCESS")
                for index in range(2)
            ]),
    )

    messages["critics_stats"] = (
        "nav2_msgs/msg/CriticsStats",
        t["nav2_msgs/msg/CriticsStats"](
            stamp=Time(sec=9, nanosec=123456789),
            critics=["RotateToGoal", "Oscillation", "GoalAlign", "PathAlign"],
            changed=np.array([True, False, True, False]),
            costs_sum=np.array([1.5, 0.0, -2.25, 0.75], dtype=np.float32)),
    )

    messages["tracking_feedback"] = (
        "nav2_msgs/msg/TrackingFeedback",
        t["nav2_msgs/msg/TrackingFeedback"](
            header=header("odom"),
            position_tracking_error=np.float32(0.05),
            heading_tracking_error=np.float32(0.125),
            current_path_index=np.uint32(7),
            robot_pose=t["geometry_msgs/msg/PoseStamped"](header=header("map"), pose=pose(1.0, 2.0)),
            distance_to_goal=np.float32(3.5),
            speed=np.float32(0.4),
            remaining_path_length=np.float32(4.25)),
    )

    messages["collision_monitor_state"] = (
        "nav2_msgs/msg/CollisionMonitorState",
        t["nav2_msgs/msg/CollisionMonitorState"](
            action_type=np.uint8(2), polygon_name="StopPolygon"),
    )

    messages["local_plan_evaluation"] = (
        "dwb_msgs/msg/LocalPlanEvaluation",
        t["dwb_msgs/msg/LocalPlanEvaluation"](
            header=header("odom"),
            twists=[trajectory_score(index) for index in range(2)],
            best_index=np.uint16(1), worst_index=np.uint16(0)),
    )

    messages["diagnostic_array"] = (
        "diagnostic_msgs/msg/DiagnosticArray",
        t["diagnostic_msgs/msg/DiagnosticArray"](
            header=header(""),
            status=[
                t["diagnostic_msgs/msg/DiagnosticStatus"](
                    level=np.uint8(0), name="controller_server: topic rate",
                    message="OK", hardware_id="shm",
                    values=[
                        t["diagnostic_msgs/msg/KeyValue"](key="rate", value="10.4"),
                        t["diagnostic_msgs/msg/KeyValue"](key="topic", value="/cmd_vel_nav"),
                    ]),
                t["diagnostic_msgs/msg/DiagnosticStatus"](
                    level=np.uint8(1), name="collision_monitor: StopPolygon",
                    message="slowdown", hardware_id="shm",
                    values=[]),
            ]),
    )

    messages["log"] = (
        "rcl_interfaces/msg/Log",
        t["rcl_interfaces/msg/Log"](
            stamp=Time(sec=12, nanosec=34), level=np.uint8(20),
            name="planner_server",
            msg="Received a goal, begin computing control effort.",
            file="nav2_planner/planner_server.cpp",
            function="computeControl", line=np.uint32(417)),
    )

    messages["parameter_event"] = (
        "rcl_interfaces/msg/ParameterEvent",
        t["rcl_interfaces/msg/ParameterEvent"](
            stamp=Time(sec=13, nanosec=5), node="/controller_server",
            new_parameters=[
                parameter("FollowPath.max_vel_x", 3, 0.5),
                parameter("FollowPath.critics", 9, ["PathAlign", "GoalAlign"]),
            ],
            changed_parameters=[parameter("use_sim_time", 1, True)],
            deleted_parameters=[parameter("obsolete", 2, 0)]),
    )

    messages["marker"] = (
        "visualization_msgs/msg/Marker",
        t["visualization_msgs/msg/Marker"](
            header=header("odom"), ns="slowdown", id=np.int32(3),
            type=np.int32(4), action=np.int32(0), pose=pose(1.25, -0.5),
            scale=vector3(0.1, 0.2, 1.0),
            color=t["std_msgs/msg/ColorRGBA"](r=np.float32(1.0), g=np.float32(0.0),
                                              b=np.float32(0.0), a=np.float32(0.5)),
            lifetime=t["builtin_interfaces/msg/Duration"](sec=0, nanosec=0),
            frame_locked=False,
            points=[point(0.0, 0.0, 0.0), point(1.0, 1.0, 0.0)],
            colors=[],
            texture_resource="",
            texture=t["sensor_msgs/msg/CompressedImage"](
                header=header("odom"), format="png", data=np.zeros(0, dtype=np.uint8)),
            uv_coordinates=[],
            text="slow",
            mesh_resource="",
            mesh_file=t["visualization_msgs/msg/MeshFile"](filename="", data=np.zeros(0, dtype=np.uint8)),
            mesh_use_embedded_materials=False),
    )

    # `<action>/_action/status`. One of the four of the 41 message types whose
    # whole payload is a single `std::vector<Message>` - with `TFMessage`,
    # `MarkerArray` and `Polygon`. `TFMessage` and `Polygon` already had a
    # fixture, so this is the third of the four; `MarkerArray` is one of the
    # three types that still has none. Two entries on purpose:
    # a `GoalStatus` is 25 bytes (16 byte uuid + two stamp words + a byte of
    # state), so the second one starts at offset 29 and its `stamp.sec` needs
    # three bytes of CDR padding - an alignment rule a single element payload
    # never reaches. The first entry is byte for byte the element inside the
    # payload `interop_matrix.py` keeps as ACTION_STATUS_FIXTURE, so this
    # fixture cross-checks those 25 bytes against three implementations instead
    # of two.
    def goal_status(uuid, sec, nanosec, status):
        return t["action_msgs/msg/GoalStatus"](
            goal_info=t["action_msgs/msg/GoalInfo"](
                goal_id=t["unique_identifier_msgs/msg/UUID"](
                    uuid=np.array(uuid, dtype=np.uint8)),
                stamp=Time(sec=sec, nanosec=nanosec)),
            status=np.int8(status))

    messages["goal_status_array"] = (
        "action_msgs/msg/GoalStatusArray",
        t["action_msgs/msg/GoalStatusArray"](status_list=[
            goal_status([1] + [0] * 15, 15145, 120000238, 2),
            goal_status(list(range(16)), 15146, 999999999, 5),
        ]),
    )

    return messages


def main() -> int:
    typestore = get_typestore(Stores.ROS2_JAZZY)
    print(f"registered {register_local_message_types(typestore)} message types "
          f"from the workspace .msg files")
    structs = load_shim_structs()
    if not structs:
        raise SystemExit(f"no generated shim headers found under {GENERATED_INCLUDE}")
    print(f"parsed {len(structs)} shim message layouts from {GENERATED_INCLUDE}")
    messages = build_messages(typestore)

    FIXTURES.mkdir(parents=True, exist_ok=True)
    lines = []
    skipped = []
    for name, (type_name, message) in sorted(messages.items()):
        try:
            cdr = typestore.serialize_cdr(message, type_name)
            shim = encode_message(structs, type_name, message)
        except Exception as error:  # a type rosbags cannot spell (see MarkerArray)
            print(f"{name:32s} {type_name:45s} SKIPPED: {error}")
            skipped.append(name)
            continue
        (FIXTURES / f"{name}.cdr").write_bytes(cdr)
        (FIXTURES / f"{name}.shim").write_bytes(shim)
        lines.append(f"{name}\t{type_name}\t{len(cdr)}\t{len(shim)}")
        print(f"{name:32s} {type_name:45s} cdr={len(cdr):6d} shim={len(shim):6d}")

    (FIXTURES / "manifest.tsv").write_text(
        "# name\ttype\tcdr_size\tshim_size\n" + "\n".join(lines) + "\n",
        encoding="utf-8", newline="\n")
    if skipped:
        print(f"\nskipped {len(skipped)}: {', '.join(skipped)}")
    print(f"\nwrote {len(lines)} fixtures to {FIXTURES}")
    return 0


if __name__ == "__main__":
    sys.exit(main())