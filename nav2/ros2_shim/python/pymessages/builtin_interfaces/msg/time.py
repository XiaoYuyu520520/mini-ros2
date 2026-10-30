from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class Time(Msg):
    _fields = [
        ('sec', f_scalar('i')),
        ('nanosec', f_scalar('I')),
    ]

    def __init__(self):
        self.sec = 0
        self.nanosec = 0
