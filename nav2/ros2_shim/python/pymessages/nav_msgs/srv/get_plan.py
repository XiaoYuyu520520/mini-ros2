from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped
from pymessages.nav_msgs.msg.path import Path

class GetPlan:

    class Request(Msg):
        _fields = [
            ('start', f_msg(PoseStamped)),
            ('goal', f_msg(PoseStamped)),
            ('tolerance', f_scalar('f')),
        ]

        def __init__(self):
            self.start = PoseStamped()
            self.goal = PoseStamped()
            self.tolerance = 0.0

    class Response(Msg):
        _fields = [
            ('plan', f_msg(Path)),
        ]

        def __init__(self):
            self.plan = Path()
