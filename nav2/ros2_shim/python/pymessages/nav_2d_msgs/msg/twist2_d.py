from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class Twist2D(Msg):
    _fields = [
        ('x', f_scalar('d')),
        ('y', f_scalar('d')),
        ('theta', f_scalar('d')),
    ]

    def __init__(self):
        self.x = 0.0
        self.y = 0.0
        self.theta = 0.0
