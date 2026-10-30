from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.wrench import Wrench
from pymessages.std_msgs.msg.header import Header

class WrenchStamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('wrench', f_msg(Wrench)),
    ]

    def __init__(self):
        self.header = Header()
        self.wrench = Wrench()
