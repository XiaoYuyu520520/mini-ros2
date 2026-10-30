from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.bounding_box import BoundingBox
from pymessages.geographic_msgs.msg.geographic_map import GeographicMap

class GetGeographicMap:

    class Request(Msg):
        _fields = [
            ('url', f_string()),
            ('bounds', f_msg(BoundingBox)),
        ]

        def __init__(self):
            self.url = ""
            self.bounds = BoundingBox()

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
            ('status', f_string()),
            ('map', f_msg(GeographicMap)),
        ]

        def __init__(self):
            self.success = False
            self.status = ""
            self.map = GeographicMap()
