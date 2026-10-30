from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.pose_with_covariance import PoseWithCovariance
from pymessages.std_msgs.msg.header import Header

class PoseWithCovarianceStamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('pose', f_msg(PoseWithCovariance)),
    ]

    def __init__(self):
        self.header = Header()
        self.pose = PoseWithCovariance()
