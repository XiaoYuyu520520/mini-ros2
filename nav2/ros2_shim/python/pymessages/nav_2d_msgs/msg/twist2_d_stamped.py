from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav_2d_msgs.msg.twist2_d import Twist2D
from pymessages.std_msgs.msg.header import Header

class Twist2DStamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('velocity', f_msg(Twist2D)),
    ]

    def __init__(self):
        self.header = Header()
        self.velocity = Twist2D()
