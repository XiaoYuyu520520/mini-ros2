from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class FrameGraph:

    class Request(Msg):

        def __init__(self):
            pass

    class Response(Msg):
        _fields = [
            ('frame_yaml', f_string()),
        ]

        def __init__(self):
            self.frame_yaml = ""
