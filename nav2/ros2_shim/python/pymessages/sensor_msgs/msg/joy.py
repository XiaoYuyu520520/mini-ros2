from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.header import Header

class Joy(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('axes', f_seq(f_scalar('f'))),
        ('buttons', f_seq(f_scalar('i'))),
    ]

    def __init__(self):
        self.header = Header()
        self.axes = []
        self.buttons = []
