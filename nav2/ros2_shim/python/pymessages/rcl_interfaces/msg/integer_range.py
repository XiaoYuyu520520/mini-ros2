from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class IntegerRange(Msg):
    _fields = [
        ('from_value', f_scalar('q')),
        ('to_value', f_scalar('q')),
        ('step', f_scalar('Q')),
    ]

    def __init__(self):
        self.from_value = 0
        self.to_value = 0
        self.step = 0
