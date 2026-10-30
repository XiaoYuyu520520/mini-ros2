from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.pose import Pose

class Particle(Msg):
    _fields = [
        ('pose', f_msg(Pose)),
        ('weight', f_scalar('d')),
    ]

    def __init__(self):
        self.pose = Pose()
        self.weight = 0.0
