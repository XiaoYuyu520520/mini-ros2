from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class ListParametersResult(Msg):
    _fields = [
        ('names', f_seq(f_string())),
        ('prefixes', f_seq(f_string())),
    ]

    def __init__(self):
        self.names = []
        self.prefixes = []
