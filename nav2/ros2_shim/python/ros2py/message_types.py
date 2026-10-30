"""``nav2_msgs/action/NavigateThroughPoses`` for a generated Python class.

The generated classes know their fields, not their ROS2 type name - the C++
side gets ``ros2_message_type`` from its generator, the Python side gets the
class path - and the name is what the graph record has to carry, because that
is what ``ros2 topic list -t`` prints and what the bridge daemon looks up
before it forwards anything.

The name is recoverable exactly: the generator snake-cases the ROS2 name into
the file name (``PoseStamped`` -> ``pose_stamped.py``), so pascal-casing it back
is the inverse, and the module path carries the package and the kind. A nested
class adds its own name to the action's (``NavigateThroughPoses_Goal``), which
is how ROS2 spells the parts of an action.
"""
from __future__ import annotations


def pascal_case(name: str) -> str:
    return "".join(part[:1].upper() + part[1:] for part in name.split("_") if part)


def message_type_name(message_type) -> str:
    """``pymessages.nav2_msgs.action.navigate_through_poses.NavigateThroughPoses``
    -> ``nav2_msgs/action/NavigateThroughPoses``.
    """
    module = getattr(message_type, "__module__", "") or ""
    parts = module.split(".")
    if not parts or parts[0] != "pymessages" or len(parts) < 3:
        return getattr(message_type, "ros2_message_type", "") or ""
    kind = parts[-2]
    package = ".".join(parts[1:-2])
    type_name = "%s/%s/%s" % (package, kind, pascal_case(parts[-1]))
    qualname = getattr(message_type, "__qualname__", "") or ""
    if "." in qualname:
        type_name += "_" + qualname.split(".", 1)[1].replace(".", "_")
    return type_name
