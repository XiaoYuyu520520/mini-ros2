from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class LaserEcho(Msg):
    _fields = [
        ('echoes', f_seq(f_scalar('f'))),
    ]

    def __init__(self):
        self.echoes = []
