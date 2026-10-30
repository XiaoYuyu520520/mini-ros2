from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration

class JointTrajectoryPoint(Msg):
    _fields = [
        ('positions', f_seq(f_scalar('d'))),
        ('velocities', f_seq(f_scalar('d'))),
        ('accelerations', f_seq(f_scalar('d'))),
        ('effort', f_seq(f_scalar('d'))),
        ('time_from_start', f_msg(Duration)),
    ]

    def __init__(self):
        self.positions = []
        self.velocities = []
        self.accelerations = []
        self.effort = []
        self.time_from_start = Duration()
