from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class ClearEntireCostmap:

    class Request(Msg):
        _fields = [
            ('plugins', f_seq(f_string())),
        ]

        def __init__(self):
            self.plugins = []

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
        ]

        def __init__(self):
            self.success = False
