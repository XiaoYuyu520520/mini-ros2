from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class AddSubmap:

    class Request(Msg):
        _fields = [
            ('filename', f_string()),
        ]

        def __init__(self):
            self.filename = ""

    class Response(Msg):

        def __init__(self):
            pass
