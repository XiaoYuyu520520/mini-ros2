from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.dwb_msgs.msg.critic_score import CriticScore
from pymessages.dwb_msgs.msg.trajectory2_d import Trajectory2D

class TrajectoryScore(Msg):
    _fields = [
        ('traj', f_msg(Trajectory2D)),
        ('scores', f_seq(f_msg(CriticScore))),
        ('total', f_scalar('f')),
    ]

    def __init__(self):
        self.traj = Trajectory2D()
        self.scores = []
        self.total = 0.0
