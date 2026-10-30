from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.geo_point import GeoPoint
from pymessages.std_msgs.msg.header import Header

class GeoPointStamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('position', f_msg(GeoPoint)),
    ]

    def __init__(self):
        self.header = Header()
        self.position = GeoPoint()
