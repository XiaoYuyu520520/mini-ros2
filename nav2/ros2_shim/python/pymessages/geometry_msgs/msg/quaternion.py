from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class Quaternion(Msg):
    _fields = [
        ('x', f_scalar('d')),
        ('y', f_scalar('d')),
        ('z', f_scalar('d')),
        ('w', f_scalar('d')),
    ]

    def __init__(self):
        self.x = 0.0
        self.y = 0.0
        self.z = 0.0
        self.w = 0.0
