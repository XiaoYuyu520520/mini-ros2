from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class Point32(Msg):
    _fields = [
        ('x', f_scalar('f')),
        ('y', f_scalar('f')),
        ('z', f_scalar('f')),
    ]

    def __init__(self):
        self.x = 0.0
        self.y = 0.0
        self.z = 0.0
