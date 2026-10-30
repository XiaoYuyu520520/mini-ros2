from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.vector3 import Vector3

class Wrench(Msg):
    _fields = [
        ('force', f_msg(Vector3)),
        ('torque', f_msg(Vector3)),
    ]

    def __init__(self):
        self.force = Vector3()
        self.torque = Vector3()
