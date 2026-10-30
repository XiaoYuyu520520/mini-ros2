from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.time import Time

class GetState:

    class Request(Msg):
        _fields = [
            ('time_stamp', f_msg(Time)),
            ('frame_id', f_string()),
        ]

        def __init__(self):
            self.time_stamp = Time()
            self.frame_id = ""

    class Response(Msg):
        _fields = [
            ('state', f_array(f_scalar('d'), 15)),
            ('covariance', f_array(f_scalar('d'), 225)),
        ]

        def __init__(self):
            self.state = [0.0] * 15
            self.covariance = [0.0] * 225
