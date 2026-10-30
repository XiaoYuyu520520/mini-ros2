from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class SerializePoseGraph:

    class Request(Msg):
        _fields = [
            ('filename', f_string()),
        ]

        def __init__(self):
            self.filename = ""

    class Response(Msg):
        RESULT_SUCCESS = 0
        RESULT_FAILED_TO_WRITE_FILE = 255
        _fields = [
            ('result', f_scalar('B')),
        ]

        def __init__(self):
            self.result = 0
