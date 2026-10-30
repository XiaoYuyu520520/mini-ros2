from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.dwb_msgs.msg.local_plan_evaluation import LocalPlanEvaluation
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped
from pymessages.nav_2d_msgs.msg.twist2_d import Twist2D
from pymessages.nav_msgs.msg.path import Path

class DebugLocalPlan:

    class Request(Msg):
        _fields = [
            ('pose', f_msg(PoseStamped)),
            ('velocity', f_msg(Twist2D)),
            ('global_plan', f_msg(Path)),
        ]

        def __init__(self):
            self.pose = PoseStamped()
            self.velocity = Twist2D()
            self.global_plan = Path()

    class Response(Msg):
        _fields = [
            ('results', f_msg(LocalPlanEvaluation)),
        ]

        def __init__(self):
            self.results = LocalPlanEvaluation()
