from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class TypeHash(Msg):
    _fields = [
        ('version', f_scalar('B')),
        ('value', f_array(f_scalar('B'), 32)),
    ]

    def __init__(self):
        self.version = 0
        self.value = [0] * 32
