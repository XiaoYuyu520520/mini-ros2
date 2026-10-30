from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class UVCoordinate(Msg):
    _fields = [
        ('u', f_scalar('f')),
        ('v', f_scalar('f')),
    ]

    def __init__(self):
        self.u = 0.0
        self.v = 0.0
