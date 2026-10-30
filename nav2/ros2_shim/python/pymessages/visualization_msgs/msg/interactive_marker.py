from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.pose import Pose
from pymessages.std_msgs.msg.header import Header
from pymessages.visualization_msgs.msg.interactive_marker_control import InteractiveMarkerControl
from pymessages.visualization_msgs.msg.menu_entry import MenuEntry

class InteractiveMarker(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('pose', f_msg(Pose)),
        ('name', f_string()),
        ('description', f_string()),
        ('scale', f_scalar('f')),
        ('menu_entries', f_seq(f_msg(MenuEntry))),
        ('controls', f_seq(f_msg(InteractiveMarkerControl))),
    ]

    def __init__(self):
        self.header = Header()
        self.pose = Pose()
        self.name = ""
        self.description = ""
        self.scale = 0.0
        self.menu_entries = []
        self.controls = []
