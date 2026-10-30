from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.vector3 import Vector3
from pymessages.std_msgs.msg.header import Header

class Vector3Stamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('vector', f_msg(Vector3)),
    ]

    def __init__(self):
        self.header = Header()
        self.vector = Vector3()
