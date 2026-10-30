from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.twist_with_covariance import TwistWithCovariance
from pymessages.std_msgs.msg.header import Header

class VelocityWithCovarianceStamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('body_frame_id', f_string()),
        ('reference_frame_id', f_string()),
        ('velocity', f_msg(TwistWithCovariance)),
    ]

    def __init__(self):
        self.header = Header()
        self.body_frame_id = ""
        self.reference_frame_id = ""
        self.velocity = TwistWithCovariance()
