from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.vector3 import Vector3
from pymessages.std_msgs.msg.header import Header

class MagneticField(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('magnetic_field', f_msg(Vector3)),
        ('magnetic_field_covariance', f_array(f_scalar('d'), 9)),
    ]

    def __init__(self):
        self.header = Header()
        self.magnetic_field = Vector3()
        self.magnetic_field_covariance = [0.0] * 9
