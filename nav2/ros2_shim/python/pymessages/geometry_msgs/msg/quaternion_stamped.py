from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.quaternion import Quaternion
from pymessages.std_msgs.msg.header import Header

class QuaternionStamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('quaternion', f_msg(Quaternion)),
    ]

    def __init__(self):
        self.header = Header()
        self.quaternion = Quaternion()
