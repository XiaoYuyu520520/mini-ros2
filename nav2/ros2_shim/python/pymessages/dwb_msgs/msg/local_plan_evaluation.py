from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.dwb_msgs.msg.trajectory_score import TrajectoryScore
from pymessages.std_msgs.msg.header import Header

class LocalPlanEvaluation(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('twists', f_seq(f_msg(TrajectoryScore))),
        ('best_index', f_scalar('H')),
        ('worst_index', f_scalar('H')),
    ]

    def __init__(self):
        self.header = Header()
        self.twists = []
        self.best_index = 0
        self.worst_index = 0
