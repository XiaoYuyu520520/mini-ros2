from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.pose_with_covariance_stamped import PoseWithCovarianceStamped

class SetPose:

    class Request(Msg):
        _fields = [
            ('pose', f_msg(PoseWithCovarianceStamped)),
        ]

        def __init__(self):
            self.pose = PoseWithCovarianceStamped()

    class Response(Msg):

        def __init__(self):
            pass
