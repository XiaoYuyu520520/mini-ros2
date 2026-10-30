from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.geo_pose import GeoPose

class SetDatum:

    class Request(Msg):
        _fields = [
            ('geo_pose', f_msg(GeoPose)),
        ]

        def __init__(self):
            self.geo_pose = GeoPose()

    class Response(Msg):

        def __init__(self):
            pass
