from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class FloatingPointRange(Msg):
    _fields = [
        ('from_value', f_scalar('d')),
        ('to_value', f_scalar('d')),
        ('step', f_scalar('d')),
    ]

    def __init__(self):
        self.from_value = 0.0
        self.to_value = 0.0
        self.step = 0.0
