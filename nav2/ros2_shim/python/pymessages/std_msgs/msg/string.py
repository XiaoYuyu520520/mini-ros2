from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class String(Msg):
    _fields = [
        ('data', f_string()),
    ]

    def __init__(self):
        self.data = ""
