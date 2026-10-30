from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.point import Point
from pymessages.std_msgs.msg.header import Header

class PointStamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('point', f_msg(Point)),
    ]

    def __init__(self):
        self.header = Header()
        self.point = Point()
