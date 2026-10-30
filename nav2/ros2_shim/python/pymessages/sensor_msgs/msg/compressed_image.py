from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.header import Header

class CompressedImage(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('format', f_string()),
        ('data', f_seq(f_scalar('B'))),
    ]

    def __init__(self):
        self.header = Header()
        self.format = ""
        self.data = []
