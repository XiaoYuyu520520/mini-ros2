from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.string import String

class SaveMap:

    class Request(Msg):
        _fields = [
            ('filename', f_msg(String)),
        ]

        def __init__(self):
            self.filename = String()

    class Response(Msg):

        def __init__(self):
            pass
