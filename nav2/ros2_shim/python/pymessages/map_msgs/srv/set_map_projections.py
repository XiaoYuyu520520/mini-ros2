from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.map_msgs.msg.projected_map_info import ProjectedMapInfo

class SetMapProjections:

    class Request(Msg):

        def __init__(self):
            pass

    class Response(Msg):
        _fields = [
            ('projected_maps_info', f_seq(f_msg(ProjectedMapInfo))),
        ]

        def __init__(self):
            self.projected_maps_info = []
