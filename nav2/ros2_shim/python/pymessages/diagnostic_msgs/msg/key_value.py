from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class KeyValue(Msg):
    _fields = [
        ('key', f_string()),
        ('value', f_string()),
    ]

    def __init__(self):
        self.key = ""
        self.value = ""
