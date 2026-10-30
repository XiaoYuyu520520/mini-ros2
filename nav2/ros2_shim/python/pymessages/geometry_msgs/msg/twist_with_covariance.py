from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.twist import Twist

class TwistWithCovariance(Msg):
    _fields = [
        ('twist', f_msg(Twist)),
        ('covariance', f_array(f_scalar('d'), 36)),
    ]

    def __init__(self):
        self.twist = Twist()
        self.covariance = [0.0] * 36
