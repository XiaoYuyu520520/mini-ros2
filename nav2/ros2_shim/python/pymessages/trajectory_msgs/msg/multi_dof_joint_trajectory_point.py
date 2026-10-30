from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration
from pymessages.geometry_msgs.msg.transform import Transform
from pymessages.geometry_msgs.msg.twist import Twist

class MultiDOFJointTrajectoryPoint(Msg):
    _fields = [
        ('transforms', f_seq(f_msg(Transform))),
        ('velocities', f_seq(f_msg(Twist))),
        ('accelerations', f_seq(f_msg(Twist))),
        ('time_from_start', f_msg(Duration)),
    ]

    def __init__(self):
        self.transforms = []
        self.velocities = []
        self.accelerations = []
        self.time_from_start = Duration()
