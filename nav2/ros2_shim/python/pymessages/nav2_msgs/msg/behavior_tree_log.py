from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.time import Time
from pymessages.nav2_msgs.msg.behavior_tree_status_change import BehaviorTreeStatusChange

class BehaviorTreeLog(Msg):
    _fields = [
        ('timestamp', f_msg(Time)),
        ('event_log', f_seq(f_msg(BehaviorTreeStatusChange))),
    ]

    def __init__(self):
        self.timestamp = Time()
        self.event_log = []
