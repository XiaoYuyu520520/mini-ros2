from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.twist import Twist
from pymessages.std_msgs.msg.header import Header

class TwistStamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('twist', f_msg(Twist)),
    ]

    def __init__(self):
        self.header = Header()
        self.twist = Twist()
