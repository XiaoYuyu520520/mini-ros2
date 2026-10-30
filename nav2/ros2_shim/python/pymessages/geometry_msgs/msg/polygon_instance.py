from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.polygon import Polygon

class PolygonInstance(Msg):
    _fields = [
        ('polygon', f_msg(Polygon)),
        ('id', f_scalar('q')),
    ]

    def __init__(self):
        self.polygon = Polygon()
        self.id = 0
