from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class EdgeCost(Msg):
    _fields = [
        ('edgeid', f_scalar('H')),
        ('cost', f_scalar('f')),
    ]

    def __init__(self):
        self.edgeid = 0
        self.cost = 0.0
