from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav_msgs.msg.trajectory_point import TrajectoryPoint
from pymessages.std_msgs.msg.header import Header

class Trajectory(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('points', f_seq(f_msg(TrajectoryPoint))),
    ]

    def __init__(self):
        self.header = Header()
        self.points = []
