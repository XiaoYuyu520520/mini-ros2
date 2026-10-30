from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.time import Time

class Header(Msg):
    _fields = [
        ('stamp', f_msg(Time)),
        ('frame_id', f_string()),
    ]

    def __init__(self):
        self.stamp = Time()
        self.frame_id = ""
