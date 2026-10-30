from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.accel import Accel

class AccelWithCovariance(Msg):
    _fields = [
        ('accel', f_msg(Accel)),
        ('covariance', f_array(f_scalar('d'), 36)),
    ]

    def __init__(self):
        self.accel = Accel()
        self.covariance = [0.0] * 36
