from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped

class ClearCostmapAroundPose:

    class Request(Msg):
        _fields = [
            ('pose', f_msg(PoseStamped)),
            ('reset_distance', f_scalar('d')),
            ('plugins', f_seq(f_string())),
        ]

        def __init__(self):
            self.pose = PoseStamped()
            self.reset_distance = 0.0
            self.plugins = []

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
        ]

        def __init__(self):
            self.success = False
