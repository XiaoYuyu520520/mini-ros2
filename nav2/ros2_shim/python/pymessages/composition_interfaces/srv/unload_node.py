from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class UnloadNode:

    class Request(Msg):
        _fields = [
            ('unique_id', f_scalar('Q')),
        ]

        def __init__(self):
            self.unique_id = 0

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
            ('error_message', f_string()),
        ]

        def __init__(self):
            self.success = False
            self.error_message = ""
