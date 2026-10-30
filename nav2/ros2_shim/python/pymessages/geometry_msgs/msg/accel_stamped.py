from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.accel import Accel
from pymessages.std_msgs.msg.header import Header

class AccelStamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('accel', f_msg(Accel)),
    ]

    def __init__(self):
        self.header = Header()
        self.accel = Accel()
