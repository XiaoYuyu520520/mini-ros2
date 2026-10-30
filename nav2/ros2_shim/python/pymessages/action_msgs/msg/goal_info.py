from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.time import Time
from pymessages.unique_identifier_msgs.msg.uuid import UUID

class GoalInfo(Msg):
    _fields = [
        ('goal_id', f_msg(UUID)),
        ('stamp', f_msg(Time)),
    ]

    def __init__(self):
        self.goal_id = UUID()
        self.stamp = Time()
