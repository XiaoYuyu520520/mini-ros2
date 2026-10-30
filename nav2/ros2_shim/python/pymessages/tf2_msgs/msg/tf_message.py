from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.transform_stamped import TransformStamped

class TFMessage(Msg):
    _fields = [
        ('transforms', f_seq(f_msg(TransformStamped))),
    ]

    def __init__(self):
        self.transforms = []
