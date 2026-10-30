from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.inertia import Inertia
from pymessages.std_msgs.msg.header import Header

class InertiaStamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('inertia', f_msg(Inertia)),
    ]

    def __init__(self):
        self.header = Header()
        self.inertia = Inertia()
