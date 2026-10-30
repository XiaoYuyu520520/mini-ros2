from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.geo_point import GeoPoint
from pymessages.geometry_msgs.msg.quaternion import Quaternion

class GeoPose(Msg):
    _fields = [
        ('position', f_msg(GeoPoint)),
        ('orientation', f_msg(Quaternion)),
    ]

    def __init__(self):
        self.position = GeoPoint()
        self.orientation = Quaternion()
