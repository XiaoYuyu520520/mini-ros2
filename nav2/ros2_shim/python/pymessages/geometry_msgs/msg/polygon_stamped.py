from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.polygon import Polygon
from pymessages.std_msgs.msg.header import Header

class PolygonStamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('polygon', f_msg(Polygon)),
    ]

    def __init__(self):
        self.header = Header()
        self.polygon = Polygon()
