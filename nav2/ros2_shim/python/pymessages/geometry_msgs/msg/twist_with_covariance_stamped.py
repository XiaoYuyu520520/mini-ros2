from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.twist_with_covariance import TwistWithCovariance
from pymessages.std_msgs.msg.header import Header

class TwistWithCovarianceStamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('twist', f_msg(TwistWithCovariance)),
    ]

    def __init__(self):
        self.header = Header()
        self.twist = TwistWithCovariance()
