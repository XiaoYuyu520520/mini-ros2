from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.geo_point import GeoPoint

class BoundingBox(Msg):
    _fields = [
        ('min_pt', f_msg(GeoPoint)),
        ('max_pt', f_msg(GeoPoint)),
    ]

    def __init__(self):
        self.min_pt = GeoPoint()
        self.max_pt = GeoPoint()
