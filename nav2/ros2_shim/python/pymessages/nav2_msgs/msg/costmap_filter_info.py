from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.header import Header

class CostmapFilterInfo(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('type', f_scalar('B')),
        ('filter_mask_topic', f_string()),
        ('base', f_scalar('f')),
        ('multiplier', f_scalar('f')),
    ]

    def __init__(self):
        self.header = Header()
        self.type = 0
        self.filter_mask_topic = ""
        self.base = 0.0
        self.multiplier = 0.0
