from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.time import Time

class BehaviorTreeStatusChange(Msg):
    _fields = [
        ('timestamp', f_msg(Time)),
        ('node_name', f_string()),
        ('uid', f_scalar('H')),
        ('previous_status', f_string()),
        ('current_status', f_string()),
    ]

    def __init__(self):
        self.timestamp = Time()
        self.node_name = ""
        self.uid = 0
        self.previous_status = ""
        self.current_status = ""
