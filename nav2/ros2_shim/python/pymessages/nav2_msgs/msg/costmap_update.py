from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.header import Header

class CostmapUpdate(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('x', f_scalar('I')),
        ('y', f_scalar('I')),
        ('size_x', f_scalar('I')),
        ('size_y', f_scalar('I')),
        ('data', f_seq(f_scalar('B'))),
    ]

    def __init__(self):
        self.header = Header()
        self.x = 0
        self.y = 0
        self.size_x = 0
        self.size_y = 0
        self.data = []
