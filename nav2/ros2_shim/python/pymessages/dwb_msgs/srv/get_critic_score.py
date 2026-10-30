from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.dwb_msgs.msg.critic_score import CriticScore
from pymessages.dwb_msgs.msg.trajectory2_d import Trajectory2D
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped
from pymessages.nav_2d_msgs.msg.twist2_d import Twist2D
from pymessages.nav_msgs.msg.path import Path

class GetCriticScore:

    class Request(Msg):
        _fields = [
            ('pose', f_msg(PoseStamped)),
            ('velocity', f_msg(Twist2D)),
            ('global_plan', f_msg(Path)),
            ('traj', f_msg(Trajectory2D)),
            ('critic_name', f_string()),
        ]

        def __init__(self):
            self.pose = PoseStamped()
            self.velocity = Twist2D()
            self.global_plan = Path()
            self.traj = Trajectory2D()
            self.critic_name = ""

    class Response(Msg):
        _fields = [
            ('score', f_msg(CriticScore)),
        ]

        def __init__(self):
            self.score = CriticScore()
