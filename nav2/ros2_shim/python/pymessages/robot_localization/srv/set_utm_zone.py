from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class SetUTMZone:

    class Request(Msg):
        _fields = [
            ('utm_zone', f_string()),
        ]

        def __init__(self):
            self.utm_zone = ""

    class Response(Msg):

        def __init__(self):
            pass
