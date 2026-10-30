from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.diagnostic_msgs.msg.key_value import KeyValue

class DiagnosticStatus(Msg):
    OK = 0
    WARN = 1
    ERROR = 2
    STALE = 3
    _fields = [
        ('level', f_scalar('B')),
        ('name', f_string()),
        ('message', f_string()),
        ('hardware_id', f_string()),
        ('values', f_seq(f_msg(KeyValue))),
    ]

    def __init__(self):
        self.level = 0
        self.name = ""
        self.message = ""
        self.hardware_id = ""
        self.values = []
