from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.vector3 import Vector3

class Inertia(Msg):
    _fields = [
        ('m', f_scalar('d')),
        ('com', f_msg(Vector3)),
        ('ixx', f_scalar('d')),
        ('ixy', f_scalar('d')),
        ('ixz', f_scalar('d')),
        ('iyy', f_scalar('d')),
        ('iyz', f_scalar('d')),
        ('izz', f_scalar('d')),
    ]

    def __init__(self):
        self.m = 0.0
        self.com = Vector3()
        self.ixx = 0.0
        self.ixy = 0.0
        self.ixz = 0.0
        self.iyy = 0.0
        self.iyz = 0.0
        self.izz = 0.0
