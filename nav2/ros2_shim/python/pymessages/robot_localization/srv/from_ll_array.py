from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.geo_point import GeoPoint
from pymessages.geometry_msgs.msg.point import Point

class FromLLArray:

    class Request(Msg):
        _fields = [
            ('ll_points', f_seq(f_msg(GeoPoint))),
        ]

        def __init__(self):
            self.ll_points = []

    class Response(Msg):
        _fields = [
            ('map_points', f_seq(f_msg(Point))),
        ]

        def __init__(self):
            self.map_points = []
