from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.point32 import Point32

class Polygon(Msg):
    _fields = [
        ('points', f_seq(f_msg(Point32))),
    ]

    def __init__(self):
        self.points = []
