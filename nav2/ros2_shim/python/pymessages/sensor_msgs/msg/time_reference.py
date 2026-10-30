from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.time import Time
from pymessages.std_msgs.msg.header import Header

class TimeReference(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('time_ref', f_msg(Time)),
        ('source', f_string()),
    ]

    def __init__(self):
        self.header = Header()
        self.time_ref = Time()
        self.source = ""
