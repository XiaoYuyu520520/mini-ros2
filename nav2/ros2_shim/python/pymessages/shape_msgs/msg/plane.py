from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class Plane(Msg):
    _fields = [
        ('coef', f_array(f_scalar('d'), 4)),
    ]

    def __init__(self):
        self.coef = [0.0] * 4
