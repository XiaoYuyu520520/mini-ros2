from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class Pause:

    class Request(Msg):

        def __init__(self):
            pass

    class Response(Msg):
        _fields = [
            ('status', f_bool()),
        ]

        def __init__(self):
            self.status = False
