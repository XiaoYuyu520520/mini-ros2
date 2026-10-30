from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.point import Point

class RouteNode(Msg):
    _fields = [
        ('nodeid', f_scalar('H')),
        ('position', f_msg(Point)),
    ]

    def __init__(self):
        self.nodeid = 0
        self.position = Point()
