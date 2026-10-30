from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.point32 import Point32

class ExclusionZoneDescription(Msg):
    _fields = [
        ('zone_name', f_string()),
        ('type', f_string()),
        ('frame_id', f_string()),
        ('points', f_seq(f_msg(Point32))),
        ('radius', f_scalar('d')),
        ('min_height', f_scalar('d')),
        ('max_height', f_scalar('d')),
        ('enabled', f_bool()),
        ('visualize', f_bool()),
        ('frame_hold_timeout', f_scalar('d')),
    ]

    def __init__(self):
        self.zone_name = ""
        self.type = ""
        self.frame_id = ""
        self.points = []
        self.radius = 0.0
        self.min_height = 0.0
        self.max_height = 0.0
        self.enabled = False
        self.visualize = False
        self.frame_hold_timeout = 0.0
