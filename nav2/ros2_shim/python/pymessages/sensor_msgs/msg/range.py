from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.header import Header

class Range(Msg):
    ULTRASOUND = 0
    INFRARED = 1
    _fields = [
        ('header', f_msg(Header)),
        ('radiation_type', f_scalar('B')),
        ('field_of_view', f_scalar('f')),
        ('min_range', f_scalar('f')),
        ('max_range', f_scalar('f')),
        ('range', f_scalar('f')),
        ('variance', f_scalar('f')),
    ]

    def __init__(self):
        self.header = Header()
        self.radiation_type = 0
        self.field_of_view = 0.0
        self.min_range = 0.0
        self.max_range = 0.0
        self.range = 0.0
        self.variance = 0.0
