from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.vector3 import Vector3

class Twist(Msg):
    _fields = [
        ('linear', f_msg(Vector3)),
        ('angular', f_msg(Vector3)),
    ]

    def __init__(self):
        self.linear = Vector3()
        self.angular = Vector3()
