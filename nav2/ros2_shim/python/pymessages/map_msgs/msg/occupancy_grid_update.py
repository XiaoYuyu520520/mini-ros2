from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.header import Header

class OccupancyGridUpdate(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('x', f_scalar('i')),
        ('y', f_scalar('i')),
        ('width', f_scalar('I')),
        ('height', f_scalar('I')),
        ('data', f_seq(f_scalar('b'))),
    ]

    def __init__(self):
        self.header = Header()
        self.x = 0
        self.y = 0
        self.width = 0
        self.height = 0
        self.data = []
