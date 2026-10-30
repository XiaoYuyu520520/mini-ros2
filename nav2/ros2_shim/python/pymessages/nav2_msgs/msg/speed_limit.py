from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.header import Header

class SpeedLimit(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('percentage', f_bool()),
        ('speed_limit', f_scalar('d')),
    ]

    def __init__(self):
        self.header = Header()
        self.percentage = False
        self.speed_limit = 0.0
