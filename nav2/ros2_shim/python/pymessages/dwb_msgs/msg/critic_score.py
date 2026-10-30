from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class CriticScore(Msg):
    _fields = [
        ('name', f_string()),
        ('raw_score', f_scalar('f')),
        ('scale', f_scalar('f')),
    ]

    def __init__(self):
        self.name = ""
        self.raw_score = 0.0
        self.scale = 0.0
