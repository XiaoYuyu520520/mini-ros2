from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class Int64(Msg):
    _fields = [
        ('data', f_scalar('q')),
    ]

    def __init__(self):
        self.data = 0
