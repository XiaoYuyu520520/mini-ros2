from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration
from pymessages.geometry_msgs.msg.pose import Pose
from pymessages.nav_2d_msgs.msg.twist2_d import Twist2D

class Trajectory2D(Msg):
    _fields = [
        ('velocity', f_msg(Twist2D)),
        ('time_offsets', f_seq(f_msg(Duration))),
        ('poses', f_seq(f_msg(Pose))),
    ]

    def __init__(self):
        self.velocity = Twist2D()
        self.time_offsets = []
        self.poses = []
