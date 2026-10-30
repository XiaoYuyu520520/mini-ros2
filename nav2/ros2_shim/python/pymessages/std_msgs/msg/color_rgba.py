from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class ColorRGBA(Msg):
    _fields = [
        ('r', f_scalar('f')),
        ('g', f_scalar('f')),
        ('b', f_scalar('f')),
        ('a', f_scalar('f')),
    ]

    def __init__(self):
        self.r = 0.0
        self.g = 0.0
        self.b = 0.0
        self.a = 0.0
