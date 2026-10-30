from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.geographic_map_changes import GeographicMapChanges

class UpdateGeographicMap:

    class Request(Msg):
        _fields = [
            ('updates', f_msg(GeographicMapChanges)),
        ]

        def __init__(self):
            self.updates = GeographicMapChanges()

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
            ('status', f_string()),
        ]

        def __init__(self):
            self.success = False
            self.status = ""
