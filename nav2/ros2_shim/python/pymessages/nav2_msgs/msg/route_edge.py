from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.point import Point

class RouteEdge(Msg):
    _fields = [
        ('edgeid', f_scalar('H')),
        ('start', f_msg(Point)),
        ('end', f_msg(Point)),
    ]

    def __init__(self):
        self.edgeid = 0
        self.start = Point()
        self.end = Point()
