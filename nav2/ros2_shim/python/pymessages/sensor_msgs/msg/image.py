from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.header import Header

class Image(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('height', f_scalar('I')),
        ('width', f_scalar('I')),
        ('encoding', f_string()),
        ('is_bigendian', f_scalar('B')),
        ('step', f_scalar('I')),
        ('data', f_seq(f_scalar('B'))),
    ]

    def __init__(self):
        self.header = Header()
        self.height = 0
        self.width = 0
        self.encoding = ""
        self.is_bigendian = 0
        self.step = 0
        self.data = []
