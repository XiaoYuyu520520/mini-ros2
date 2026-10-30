from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav2_msgs.msg.circle_object import CircleObject
from pymessages.nav2_msgs.msg.polygon_object import PolygonObject

class GetShapes:

    class Request(Msg):

        def __init__(self):
            pass

    class Response(Msg):
        _fields = [
            ('circles', f_seq(f_msg(CircleObject))),
            ('polygons', f_seq(f_msg(PolygonObject))),
        ]

        def __init__(self):
            self.circles = []
            self.polygons = []
