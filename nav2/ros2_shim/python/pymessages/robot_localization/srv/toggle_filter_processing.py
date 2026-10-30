from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class ToggleFilterProcessing:

    class Request(Msg):
        _fields = [
            ('on', f_bool()),
        ]

        def __init__(self):
            self.on = False

    class Response(Msg):
        _fields = [
            ('status', f_bool()),
        ]

        def __init__(self):
            self.status = False
