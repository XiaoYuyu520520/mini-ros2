from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class ReloadDockDatabase:

    class Request(Msg):
        _fields = [
            ('filepath', f_string()),
        ]

        def __init__(self):
            self.filepath = ""

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
        ]

        def __init__(self):
            self.success = False
