from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.polygon_instance import PolygonInstance
from pymessages.std_msgs.msg.header import Header

class PolygonInstanceStamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('polygon', f_msg(PolygonInstance)),
    ]

    def __init__(self):
        self.header = Header()
        self.polygon = PolygonInstance()
