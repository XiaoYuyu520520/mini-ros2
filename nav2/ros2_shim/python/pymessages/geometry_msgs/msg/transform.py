from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.quaternion import Quaternion
from pymessages.geometry_msgs.msg.vector3 import Vector3

class Transform(Msg):
    _fields = [
        ('translation', f_msg(Vector3)),
        ('rotation', f_msg(Quaternion)),
    ]

    def __init__(self):
        self.translation = Vector3()
        self.rotation = Quaternion()
