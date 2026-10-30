from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.pose import Pose

class PoseWithCovariance(Msg):
    _fields = [
        ('pose', f_msg(Pose)),
        ('covariance', f_array(f_scalar('d'), 36)),
    ]

    def __init__(self):
        self.pose = Pose()
        self.covariance = [0.0] * 36
