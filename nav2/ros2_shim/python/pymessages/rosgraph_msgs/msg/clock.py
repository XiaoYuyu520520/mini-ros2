from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.time import Time

class Clock(Msg):
    _fields = [
        ('clock', f_msg(Time)),
    ]

    def __init__(self):
        self.clock = Time()
