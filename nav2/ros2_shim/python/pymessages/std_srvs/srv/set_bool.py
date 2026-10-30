from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class SetBool:

    class Request(Msg):
        _fields = [
            ('data', f_bool()),
        ]

        def __init__(self):
            self.data = False

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
            ('message', f_string()),
        ]

        def __init__(self):
            self.success = False
            self.message = ""
