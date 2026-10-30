from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class Reset:

    class Request(Msg):
        _fields = [
            ('pause_new_measurements', f_bool()),
        ]

        def __init__(self):
            self.pause_new_measurements = False

    class Response(Msg):
        RESULT_SUCCESS = 0
        _fields = [
            ('result', f_scalar('B')),
        ]

        def __init__(self):
            self.result = 0
