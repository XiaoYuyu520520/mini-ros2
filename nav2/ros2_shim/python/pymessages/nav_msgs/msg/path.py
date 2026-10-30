from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped
from pymessages.std_msgs.msg.header import Header

class Path(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('poses', f_seq(f_msg(PoseStamped))),
    ]

    def __init__(self):
        self.header = Header()
        self.poses = []
