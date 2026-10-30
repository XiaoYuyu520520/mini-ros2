from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class StatisticDataPoint(Msg):
    _fields = [
        ('data_type', f_scalar('B')),
        ('data', f_scalar('d')),
    ]

    def __init__(self):
        self.data_type = 0
        self.data = 0.0
