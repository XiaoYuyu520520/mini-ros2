from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.point import Point
from pymessages.geometry_msgs.msg.pose import Pose
from pymessages.std_msgs.msg.header import Header

class InteractiveMarkerFeedback(Msg):
    KEEP_ALIVE = 0
    POSE_UPDATE = 1
    MENU_SELECT = 2
    BUTTON_CLICK = 3
    MOUSE_DOWN = 4
    MOUSE_UP = 5
    _fields = [
        ('header', f_msg(Header)),
        ('client_id', f_string()),
        ('marker_name', f_string()),
        ('control_name', f_string()),
        ('event_type', f_scalar('B')),
        ('pose', f_msg(Pose)),
        ('menu_entry_id', f_scalar('I')),
        ('mouse_point', f_msg(Point)),
        ('mouse_point_valid', f_bool()),
    ]

    def __init__(self):
        self.header = Header()
        self.client_id = ""
        self.marker_name = ""
        self.control_name = ""
        self.event_type = 0
        self.pose = Pose()
        self.menu_entry_id = 0
        self.mouse_point = Point()
        self.mouse_point_valid = False
