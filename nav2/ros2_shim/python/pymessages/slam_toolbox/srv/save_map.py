from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.string import String

class SaveMap:

    class Request(Msg):
        _fields = [
            ('name', f_msg(String)),
        ]

        def __init__(self):
            self.name = String()

    class Response(Msg):
        RESULT_SUCCESS = 0
        RESULT_NO_MAP_RECEIEVD = 1
        RESULT_UNDEFINED_FAILURE = 255
        _fields = [
            ('result', f_scalar('B')),
        ]

        def __init__(self):
            self.result = 0
