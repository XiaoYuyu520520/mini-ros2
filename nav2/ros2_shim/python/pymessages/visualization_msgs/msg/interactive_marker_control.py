from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.quaternion import Quaternion
from pymessages.visualization_msgs.msg.marker import Marker

class InteractiveMarkerControl(Msg):
    INHERIT = 0
    FIXED = 1
    VIEW_FACING = 2
    NONE = 0
    MENU = 1
    BUTTON = 2
    MOVE_AXIS = 3
    MOVE_PLANE = 4
    ROTATE_AXIS = 5
    MOVE_ROTATE = 6
    MOVE_3D = 7
    ROTATE_3D = 8
    MOVE_ROTATE_3D = 9
    _fields = [
        ('name', f_string()),
        ('orientation', f_msg(Quaternion)),
        ('orientation_mode', f_scalar('B')),
        ('interaction_mode', f_scalar('B')),
        ('always_visible', f_bool()),
        ('markers', f_seq(f_msg(Marker))),
        ('independent_marker_orientation', f_bool()),
        ('description', f_string()),
    ]

    def __init__(self):
        self.name = ""
        self.orientation = Quaternion()
        self.orientation_mode = 0
        self.interaction_mode = 0
        self.always_visible = False
        self.markers = []
        self.independent_marker_orientation = False
        self.description = ""
