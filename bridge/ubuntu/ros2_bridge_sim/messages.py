"""CDR codecs for the ROS2 message types used by the simulated robot.

Field order and field types come from the generated C++ headers in
``nav2/ros2_shim/generated/include/<pkg>/msg/*.hpp`` -- those headers are the
contract, this module is a mechanical translation of them.

Messages are plain dictionaries, e.g.::

    {"header": {"stamp": {"sec": 1, "nanosec": 0}, "frame_id": "odom"},
     "child_frame_id": "base_link", "pose": {...}, "twist": {...}}

Helpers such as :func:`make_header` / :func:`make_pose` build the common
nested shapes.
"""

from __future__ import annotations

import math
import struct
from functools import lru_cache
from typing import Any, Iterable

from .cdr import (
    PRIMITIVES, STRING_ALIGNMENT, SEQUENCE_ALIGNMENT, MIN_CDR_BODY, CdrError, CdrReader,
    CdrWriter,
)

# --------------------------------------------------------------------------
# Type names
# --------------------------------------------------------------------------

TIME = "builtin_interfaces/msg/Time"
HEADER = "std_msgs/msg/Header"
STRING = "std_msgs/msg/String"
VECTOR3 = "geometry_msgs/msg/Vector3"
POINT = "geometry_msgs/msg/Point"
QUATERNION = "geometry_msgs/msg/Quaternion"
POSE = "geometry_msgs/msg/Pose"
POSE_WITH_COVARIANCE = "geometry_msgs/msg/PoseWithCovariance"
TWIST = "geometry_msgs/msg/Twist"
TWIST_WITH_COVARIANCE = "geometry_msgs/msg/TwistWithCovariance"
POSE_STAMPED = "geometry_msgs/msg/PoseStamped"
POSE_WITH_COVARIANCE_STAMPED = "geometry_msgs/msg/PoseWithCovarianceStamped"
TRANSFORM = "geometry_msgs/msg/Transform"
TRANSFORM_STAMPED = "geometry_msgs/msg/TransformStamped"
MAP_META_DATA = "nav_msgs/msg/MapMetaData"
OCCUPANCY_GRID = "nav_msgs/msg/OccupancyGrid"
ODOMETRY = "nav_msgs/msg/Odometry"
PATH = "nav_msgs/msg/Path"
LASER_SCAN = "sensor_msgs/msg/LaserScan"
TF_MESSAGE = "tf2_msgs/msg/TFMessage"
CLOCK = "rosgraph_msgs/msg/Clock"
# A node's own log line - the topic the Windows shim publishes so `RCLCPP_INFO`
# and friends leave the process (nav2/ros2_shim/include/rclcpp/logging.hpp). It
# reaches the robot through the bridge's uplink, and "the robot can read nav2's
# own log" is the only reason it is worth a schema here.
RCL_LOG = "rcl_interfaces/msg/Log"
# The two halves of a `builtin_interfaces/Duration`, which every nav2 behavior
# goal carries as its time allowance, plus the three message types the nav2
# actions below nest.
DURATION = "builtin_interfaces/msg/Duration"
STD_EMPTY = "std_msgs/msg/Empty"
TRACKING_FEEDBACK = "nav2_msgs/msg/TrackingFeedback"
WAYPOINT_STATUS = "nav2_msgs/msg/WaypointStatus"

# --- service request/response pairs ---------------------------------------
# ROS2 names the two halves of a service `<srv>_Request` / `<srv>_Response`;
# the same convention is used here so the type strings on the wire line up
# with what a real ROS2 graph would report.
EMPTY_REQUEST = "std_srvs/srv/Empty_Request"
EMPTY_RESPONSE = "std_srvs/srv/Empty_Response"
TRIGGER_REQUEST = "std_srvs/srv/Trigger_Request"
TRIGGER_RESPONSE = "std_srvs/srv/Trigger_Response"
SET_BOOL_REQUEST = "std_srvs/srv/SetBool_Request"
SET_BOOL_RESPONSE = "std_srvs/srv/SetBool_Response"
SET_INITIAL_POSE_REQUEST = "nav2_msgs/srv/SetInitialPose_Request"
SET_INITIAL_POSE_RESPONSE = "nav2_msgs/srv/SetInitialPose_Response"
TOGGLE_REQUEST = "nav2_msgs/srv/Toggle_Request"
TOGGLE_RESPONSE = "nav2_msgs/srv/Toggle_Response"

#: Service type -> (request type, response type).
SERVICE_TYPES: dict[str, tuple[str, str]] = {
    "std_srvs/srv/Empty": (EMPTY_REQUEST, EMPTY_RESPONSE),
    "std_srvs/srv/Trigger": (TRIGGER_REQUEST, TRIGGER_RESPONSE),
    "std_srvs/srv/SetBool": (SET_BOOL_REQUEST, SET_BOOL_RESPONSE),
    # The two nav2 services a robot serves: "put yourself at this pose" and
    # "stop doing that". Both are answered by the sim, and both are what the
    # bridge needs a codec for to carry them at all.
    "nav2_msgs/srv/SetInitialPose": (SET_INITIAL_POSE_REQUEST, SET_INITIAL_POSE_RESPONSE),
    "nav2_msgs/srv/Toggle": (TOGGLE_REQUEST, TOGGLE_RESPONSE),
}

# --- action goal/feedback/result triples ----------------------------------
DRIVE_GOAL = "ros2_bridge_sim/action/Drive_Goal"
DRIVE_FEEDBACK = "ros2_bridge_sim/action/Drive_Feedback"
DRIVE_RESULT = "ros2_bridge_sim/action/Drive_Result"

#: Action type -> (goal type, feedback type, result type).
#:
#: The nav2 entries are the actions a robot that runs nav2 *on board* serves:
#: the five behaviors `nav2_behaviors` ships (Spin, BackUp, DriveOnHeading,
#: Wait) and the navigator on top of them. They are the same three payload
#: types the Windows side transcodes (bridge/windows/src/action_registry.cpp),
#: so a goal sent from one machine arrives on the other as the goal a native
#: `rclcpp_action::Client` / `rclpy.action.ActionClient` would have sent.
ACTION_TYPES: dict[str, tuple[str, str, str]] = {
    "ros2_bridge_sim/action/Drive": (DRIVE_GOAL, DRIVE_FEEDBACK, DRIVE_RESULT),
}

# The nav2 action names, spelled once so services.py and the tests agree.
SPIN = "nav2_msgs/action/Spin"
BACK_UP = "nav2_msgs/action/BackUp"
DRIVE_ON_HEADING = "nav2_msgs/action/DriveOnHeading"
WAIT = "nav2_msgs/action/Wait"
NAVIGATE_TO_POSE = "nav2_msgs/action/NavigateToPose"
COMPUTE_PATH_TO_POSE = "nav2_msgs/action/ComputePathToPose"
FOLLOW_PATH = "nav2_msgs/action/FollowPath"
FOLLOW_WAYPOINTS = "nav2_msgs/action/FollowWaypoints"

#: `<pkg>/action/<Name>` -> `<pkg>/action/<Name>_<Part>`, the naming rule ROS2
#: uses for the three halves of an action. Applied once here rather than typed
#: out, for the same reason canonical_action_type() exists on the C++ side.
def _action_parts(name: str) -> tuple[str, str, str]:
    return (name + "_Goal", name + "_Feedback", name + "_Result")


for _name in (
    SPIN, BACK_UP, DRIVE_ON_HEADING, WAIT,
    NAVIGATE_TO_POSE, COMPUTE_PATH_TO_POSE, FOLLOW_PATH, FOLLOW_WAYPOINTS,
):
    ACTION_TYPES[_name] = _action_parts(_name)
del _name

#: A ``string`` field is encoded by hand, so it is not a primitive here.
STRING_TYPE = "string"


def Seq(element: Any) -> tuple:
    """``element[]``: a 4 byte count followed by the elements."""
    return ("seq", element)


def Arr(element: Any, count: int) -> tuple:
    """``element[count]``: no count, elements back to back."""
    return ("array", element, count)


# --------------------------------------------------------------------------
# Schemas -- one entry per message, fields in declaration order
# --------------------------------------------------------------------------

SCHEMAS: dict[str, list[tuple[str, Any]]] = {
    TIME: [("sec", "int32"), ("nanosec", "uint32")],
    HEADER: [("stamp", TIME), ("frame_id", STRING_TYPE)],
    STRING: [("data", STRING_TYPE)],
    VECTOR3: [("x", "float64"), ("y", "float64"), ("z", "float64")],
    POINT: [("x", "float64"), ("y", "float64"), ("z", "float64")],
    QUATERNION: [("x", "float64"), ("y", "float64"), ("z", "float64"), ("w", "float64")],
    POSE: [("position", POINT), ("orientation", QUATERNION)],
    POSE_WITH_COVARIANCE: [("pose", POSE), ("covariance", Arr("float64", 36))],
    TWIST: [("linear", VECTOR3), ("angular", VECTOR3)],
    TWIST_WITH_COVARIANCE: [("twist", TWIST), ("covariance", Arr("float64", 36))],
    POSE_STAMPED: [("header", HEADER), ("pose", POSE)],
    POSE_WITH_COVARIANCE_STAMPED: [("header", HEADER), ("pose", POSE_WITH_COVARIANCE)],
    TRANSFORM: [("translation", VECTOR3), ("rotation", QUATERNION)],
    TRANSFORM_STAMPED: [
        ("header", HEADER),
        ("child_frame_id", STRING_TYPE),
        ("transform", TRANSFORM),
    ],
    MAP_META_DATA: [
        ("map_load_time", TIME),
        ("resolution", "float32"),
        ("width", "uint32"),
        ("height", "uint32"),
        ("origin", POSE),
    ],
    OCCUPANCY_GRID: [("header", HEADER), ("info", MAP_META_DATA), ("data", Seq("int8"))],
    ODOMETRY: [
        ("header", HEADER),
        ("child_frame_id", STRING_TYPE),
        ("pose", POSE_WITH_COVARIANCE),
        ("twist", TWIST_WITH_COVARIANCE),
    ],
    PATH: [("header", HEADER), ("poses", Seq(POSE_STAMPED))],
    LASER_SCAN: [
        ("header", HEADER),
        ("angle_min", "float32"),
        ("angle_max", "float32"),
        ("angle_increment", "float32"),
        ("time_increment", "float32"),
        ("scan_time", "float32"),
        ("range_min", "float32"),
        ("range_max", "float32"),
        ("ranges", Seq("float32")),
        ("intensities", Seq("float32")),
    ],
    TF_MESSAGE: [("transforms", Seq(TRANSFORM_STAMPED))],
    CLOCK: [("clock", TIME)],
    # Field order is the declaration order in `Log.msg`, with the five
    # severity constants (DEBUG..FATAL) left out - they are constants, not
    # fields, and nothing on the wire carries them.
    RCL_LOG: [
        ("stamp", TIME),
        ("level", "uint8"),
        ("name", STRING_TYPE),
        ("msg", STRING_TYPE),
        ("file", STRING_TYPE),
        ("function", STRING_TYPE),
        ("line", "uint32"),
    ],
    EMPTY_REQUEST: [],
    EMPTY_RESPONSE: [],
    TRIGGER_REQUEST: [],
    TRIGGER_RESPONSE: [("success", "bool"), ("message", STRING_TYPE)],
    SET_BOOL_REQUEST: [("data", "bool")],
    SET_BOOL_RESPONSE: [("success", "bool"), ("message", STRING_TYPE)],
    SET_INITIAL_POSE_REQUEST: [("pose", POSE_WITH_COVARIANCE_STAMPED)],
    # A response with no fields: the answer *is* that it came back.
    SET_INITIAL_POSE_RESPONSE: [],
    TOGGLE_REQUEST: [("enable", "bool")],
    TOGGLE_RESPONSE: [("success", "bool"), ("message", STRING_TYPE)],
    DRIVE_GOAL: [("distance", "float64")],
    DRIVE_FEEDBACK: [("travelled", "float64"), ("remaining", "float64")],
    DRIVE_RESULT: [("travelled", "float64"), ("success", "bool")],
    DURATION: [("sec", "int32"), ("nanosec", "uint32")],
    STD_EMPTY: [],
    TRACKING_FEEDBACK: [
        ("header", HEADER),
        ("position_tracking_error", "float32"),
        ("heading_tracking_error", "float32"),
        ("current_path_index", "uint32"),
        ("robot_pose", POSE_STAMPED),
        ("distance_to_goal", "float32"),
        ("speed", "float32"),
        ("remaining_path_length", "float32"),
    ],
    WAYPOINT_STATUS: [
        ("waypoint_status", "uint8"),
        ("waypoint_index", "uint32"),
        ("waypoint_pose", POSE_STAMPED),
        ("error_code", "uint16"),
        ("error_msg", STRING_TYPE),
    ],
    # -- nav2 behaviors ---------------------------------------------------
    # Spin: turn in place to `target_yaw`.
    "nav2_msgs/action/Spin_Goal": [
        ("target_yaw", "float32"),
        ("time_allowance", DURATION),
        ("disable_collision_checks", "bool"),
    ],
    "nav2_msgs/action/Spin_Feedback": [("angular_distance_traveled", "float32")],
    "nav2_msgs/action/Spin_Result": [
        ("total_elapsed_time", DURATION),
        ("error_code", "uint16"),
        ("error_msg", STRING_TYPE),
    ],
    # BackUp / DriveOnHeading share a message layout upstream: `target.x` is
    # the signed distance to travel along the current heading.
    "nav2_msgs/action/BackUp_Goal": [
        ("target", POINT),
        ("speed", "float32"),
        ("time_allowance", DURATION),
        ("disable_collision_checks", "bool"),
    ],
    "nav2_msgs/action/BackUp_Feedback": [("distance_traveled", "float32")],
    "nav2_msgs/action/BackUp_Result": [
        ("total_elapsed_time", DURATION),
        ("error_code", "uint16"),
        ("error_msg", STRING_TYPE),
    ],
    "nav2_msgs/action/DriveOnHeading_Goal": [
        ("target", POINT),
        ("speed", "float32"),
        ("time_allowance", DURATION),
        ("disable_collision_checks", "bool"),
    ],
    "nav2_msgs/action/DriveOnHeading_Feedback": [("distance_traveled", "float32")],
    "nav2_msgs/action/DriveOnHeading_Result": [
        ("total_elapsed_time", DURATION),
        ("error_code", "uint16"),
        ("error_msg", STRING_TYPE),
    ],
    # Wait: do nothing for `time`, reporting what is left.
    "nav2_msgs/action/Wait_Goal": [("time", DURATION)],
    "nav2_msgs/action/Wait_Feedback": [("time_left", DURATION)],
    "nav2_msgs/action/Wait_Result": [
        ("total_elapsed_time", DURATION),
        ("error_code", "uint16"),
        ("error_msg", STRING_TYPE),
    ],
    # -- the navigator ----------------------------------------------------
    "nav2_msgs/action/NavigateToPose_Goal": [
        ("pose", POSE_STAMPED),
        ("behavior_tree", STRING_TYPE),
    ],
    "nav2_msgs/action/NavigateToPose_Feedback": [
        ("current_pose", POSE_STAMPED),
        ("navigation_time", DURATION),
        ("estimated_time_remaining", DURATION),
        ("number_of_recoveries", "int16"),
        ("distance_remaining", "float32"),
        ("position_tracking_error", "float32"),
        ("heading_tracking_error", "float32"),
    ],
    "nav2_msgs/action/NavigateToPose_Result": [
        ("error_code", "uint16"),
        ("error_msg", STRING_TYPE),
    ],
    "nav2_msgs/action/ComputePathToPose_Goal": [
        ("goal", POSE_STAMPED),
        ("start", POSE_STAMPED),
        ("viapoints", Seq(POSE_STAMPED)),
        ("planner_id", STRING_TYPE),
        ("use_start", "bool"),
    ],
    # A feedback message with no fields at all: legal in ROS2, and the empty
    # list is what stops the codec from inventing a payload for it.
    "nav2_msgs/action/ComputePathToPose_Feedback": [],
    "nav2_msgs/action/ComputePathToPose_Result": [
        ("path", PATH),
        ("planning_time", DURATION),
        ("error_code", "uint16"),
        ("error_msg", STRING_TYPE),
    ],
    "nav2_msgs/action/FollowPath_Goal": [
        ("path", PATH),
        ("controller_id", STRING_TYPE),
        ("goal_checker_id", STRING_TYPE),
        ("progress_checker_id", STRING_TYPE),
        ("path_handler_id", STRING_TYPE),
    ],
    "nav2_msgs/action/FollowPath_Feedback": [("tracking_feedback", TRACKING_FEEDBACK)],
    "nav2_msgs/action/FollowPath_Result": [
        ("result", STD_EMPTY),
        ("error_code", "uint16"),
        ("error_msg", STRING_TYPE),
    ],
    "nav2_msgs/action/FollowWaypoints_Goal": [
        ("number_of_loops", "uint32"),
        ("goal_index", "uint32"),
        ("poses", Seq(POSE_STAMPED)),
    ],
    "nav2_msgs/action/FollowWaypoints_Feedback": [("current_waypoint", "uint32")],
    "nav2_msgs/action/FollowWaypoints_Result": [
        ("missed_waypoints", Seq(WAYPOINT_STATUS)),
        ("error_code", "uint16"),
        ("error_msg", STRING_TYPE),
    ],
}

#: Every type this module can encode/decode.
SUPPORTED_TYPES = frozenset(SCHEMAS)


class UnsupportedType(KeyError):
    """No schema is known for the requested message type."""


# --------------------------------------------------------------------------
# Alignment
# --------------------------------------------------------------------------

@lru_cache(maxsize=None)
def alignment_of(type_name: str) -> int:
    """Required alignment of a message type.

    CDR1 does not pad in front of a nested struct: a struct member is aligned
    exactly like its first field (``rosbags``: ``align(NAME) ==
    align(fields[0])``).
    """
    fields = SCHEMAS.get(type_name)
    if not fields:
        if type_name in PRIMITIVES:
            return PRIMITIVES[type_name][0]
        raise UnsupportedType(type_name)
    return spec_alignment(fields[0][1])


@lru_cache(maxsize=None)
def spec_alignment(spec: Any) -> int:
    """Required alignment of a field spec."""
    if isinstance(spec, tuple):
        if spec[0] == "seq":
            return SEQUENCE_ALIGNMENT
        return spec_alignment(spec[1])  # array: aligned like its element
    if spec == STRING_TYPE:
        return STRING_ALIGNMENT
    if spec in PRIMITIVES:
        return PRIMITIVES[spec][0]
    return alignment_of(spec)


# --------------------------------------------------------------------------
# Encoding
# --------------------------------------------------------------------------

def encode(message: dict, type_name: str) -> bytes:
    """Encode ``message`` to CDR bytes, including the encapsulation header."""
    writer = CdrWriter()
    _write(writer, type_name, message)
    return writer.to_cdr()


def _write(writer: CdrWriter, type_name: str, message: dict) -> None:
    fields = SCHEMAS.get(type_name)
    if fields is None:
        raise UnsupportedType(type_name)
    if not isinstance(message, dict):
        raise CdrError("expected a dict for %s, got %s" % (type_name, type(message).__name__))
    for name, spec in fields:
        if name not in message:
            raise CdrError("missing field %r in %s" % (name, type_name))
        _write_field(writer, spec, message[name], "%s.%s" % (type_name, name))


def _write_field(writer: CdrWriter, spec: Any, value: Any, where: str) -> None:
    if isinstance(spec, tuple):
        kind = spec[0]
        if kind == "seq":
            _write_sequence(writer, spec[1], value, where)
        else:
            _write_array(writer, spec[1], spec[2], value, where)
        return
    if spec == STRING_TYPE:
        writer.string(_as_str(value, where))
        return
    if spec in PRIMITIVES:
        try:
            writer.prim(spec, value)
        except (struct.error, TypeError, ValueError) as exc:
            raise CdrError("bad %s value %r for %s: %s" % (spec, value, where, exc)) from exc
        return
    _write(writer, spec, value)


def _write_sequence(writer: CdrWriter, element: Any, values: Iterable, where: str) -> None:
    items = values if isinstance(values, (list, tuple)) else list(values)
    writer.sequence_length(len(items))
    if isinstance(element, str) and element in PRIMITIVES:
        writer.prim_array(element, items)
        return
    if not items:
        return
    alignment = spec_alignment(element)
    for index, item in enumerate(items):
        writer.align(alignment)
        _write_field(writer, element, item, "%s[%d]" % (where, index))


def _write_array(writer: CdrWriter, element: Any, count: int, values: Iterable, where: str) -> None:
    items = values if isinstance(values, (list, tuple)) else list(values)
    if len(items) != count:
        raise CdrError("%s expects %d elements, got %d" % (where, count, len(items)))
    if isinstance(element, str) and element in PRIMITIVES:
        writer.prim_array(element, items)
        return
    alignment = spec_alignment(element)
    for index, item in enumerate(items):
        writer.align(alignment)
        _write_field(writer, element, item, "%s[%d]" % (where, index))


# --------------------------------------------------------------------------
# Decoding
# --------------------------------------------------------------------------

def decode(data: bytes | bytearray | memoryview, type_name: str) -> dict:
    """Decode CDR bytes (with encapsulation header) into a dict.

    A body shorter than :data:`~.cdr.MIN_CDR_BODY` is allowed to carry the zero
    padding every real ROS2 peer adds (see that constant for the measurement),
    and nothing else is. The padding is not optional here: it is what
    ``bridge/windows/include/ros2_bridge/transcode.hpp`` sends for a field-less
    message, so a strict "no trailing bytes" check made every
    ``std_srvs/srv/Trigger`` and ``std_srvs/srv/Empty`` request that arrived
    over the bridge undecodable - the caller saw a five second timeout and
    "the service did not run", and the agent's error named the *type* rather
    than the length. Accepting exactly ``MIN_CDR_BODY`` bytes' worth of body
    keeps the check severe enough to still reject a genuinely oversized
    payload.
    """
    reader = CdrReader(data)
    message = _read(reader, type_name)
    if not reader.at_end:
        consumed = len(reader) - reader.remaining
        padding = MIN_CDR_BODY - consumed
        trailing = reader.raw(reader.remaining)
        # `len(trailing)`, not `reader.remaining`: `raw()` has already advanced
        # the reader by the time this line runs.
        if consumed >= MIN_CDR_BODY or len(trailing) != padding or any(trailing):
            raise CdrError("%d trailing byte(s) after %s" % (len(trailing), type_name))
    return message


def _read(reader: CdrReader, type_name: str) -> dict:
    fields = SCHEMAS.get(type_name)
    if fields is None:
        raise UnsupportedType(type_name)
    return {name: _read_field(reader, spec, "%s.%s" % (type_name, name)) for name, spec in fields}


def _read_field(reader: CdrReader, spec: Any, where: str) -> Any:
    if isinstance(spec, tuple):
        if spec[0] == "seq":
            count = reader.sequence_length()
            if isinstance(spec[1], str) and spec[1] in PRIMITIVES:
                return reader.prim_array(spec[1], count)
            return [_read_field(reader, spec[1], "%s[%d]" % (where, i)) for i in range(count)]
        count = spec[2]
        if isinstance(spec[1], str) and spec[1] in PRIMITIVES:
            values = reader.prim_array(spec[1], count)
            if len(values) != count:
                raise CdrError("%s: truncated fixed array" % where)
            return values
        return [_read_field(reader, spec[1], "%s[%d]" % (where, i)) for i in range(count)]
    if spec == STRING_TYPE:
        return reader.string()
    if spec in PRIMITIVES:
        try:
            return reader.prim(spec)
        except CdrError as exc:
            raise CdrError("%s: %s" % (where, exc)) from exc
    return _read(reader, spec)


# --------------------------------------------------------------------------
# Small builders for the shapes the simulator needs
# --------------------------------------------------------------------------

def make_time(sec: int, nanosec: int = 0) -> dict:
    return {"sec": int(sec), "nanosec": int(nanosec)}


def make_header(frame_id: str, sec: int = 0, nanosec: int = 0) -> dict:
    return {"stamp": make_time(sec, nanosec), "frame_id": frame_id}


def yaw_to_quaternion(yaw: float) -> dict:
    return {"x": 0.0, "y": 0.0, "z": math.sin(yaw / 2.0), "w": math.cos(yaw / 2.0)}


def quaternion_to_yaw(quaternion: dict) -> float:
    return math.atan2(
        2.0 * (quaternion["w"] * quaternion["z"] + quaternion["x"] * quaternion["y"]),
        1.0 - 2.0 * (quaternion["y"] ** 2 + quaternion["z"] ** 2),
    )


def make_pose(x: float, y: float, yaw: float = 0.0, z: float = 0.0) -> dict:
    return {"position": {"x": x, "y": y, "z": z}, "orientation": yaw_to_quaternion(yaw)}


def make_twist(linear_x: float = 0.0, angular_z: float = 0.0) -> dict:
    return {
        "linear": {"x": linear_x, "y": 0.0, "z": 0.0},
        "angular": {"x": 0.0, "y": 0.0, "z": angular_z},
    }


def make_transform_stamped(
    parent: str,
    child: str,
    x: float = 0.0,
    y: float = 0.0,
    z: float = 0.0,
    yaw: float = 0.0,
    sec: int = 0,
    nanosec: int = 0,
) -> dict:
    """A TransformStamped with a yaw-only rotation."""
    return {
        "header": make_header(parent, sec, nanosec),
        "child_frame_id": child,
        "transform": {
            "translation": {"x": x, "y": y, "z": z},
            "rotation": yaw_to_quaternion(yaw),
        },
    }


def diagonal36(*values: float) -> list[float]:
    """Covariance with the given values on the 6x6 diagonal."""
    out = [0.0] * 36
    for index, value in enumerate(values):
        out[index * 6 + index] = value
    return out


def _as_str(value: Any, where: str) -> str:
    if isinstance(value, str):
        return value
    if isinstance(value, bytes):
        return value.decode("utf-8")
    raise CdrError("%s expects a string, got %s" % (where, type(value).__name__))

