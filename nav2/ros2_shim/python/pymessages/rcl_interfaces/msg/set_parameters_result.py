from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class SetParametersResult(Msg):
    _fields = [
        ('successful', f_bool()),
        ('reason', f_string()),
    ]

    def __init__(self):
        self.successful = False
        self.reason = ""
