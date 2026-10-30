from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.transform import Transform
from pymessages.std_msgs.msg.header import Header

class TransformStamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('child_frame_id', f_string()),
        ('transform', f_msg(Transform)),
    ]

    def __init__(self):
        self.header = Header()
        self.child_frame_id = ""
        self.transform = Transform()
