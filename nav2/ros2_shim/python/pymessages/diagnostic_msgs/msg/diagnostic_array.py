from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.diagnostic_msgs.msg.diagnostic_status import DiagnosticStatus
from pymessages.std_msgs.msg.header import Header

class DiagnosticArray(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('status', f_seq(f_msg(DiagnosticStatus))),
    ]

    def __init__(self):
        self.header = Header()
        self.status = []
