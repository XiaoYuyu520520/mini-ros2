from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.point import Point
from pymessages.geometry_msgs.msg.quaternion import Quaternion

class Pose(Msg):
    _fields = [
        ('position', f_msg(Point)),
        ('orientation', f_msg(Quaternion)),
    ]

    def __init__(self):
        self.position = Point()
        self.orientation = Quaternion()
