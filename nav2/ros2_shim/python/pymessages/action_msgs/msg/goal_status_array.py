from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.action_msgs.msg.goal_status import GoalStatus

class GoalStatusArray(Msg):
    _fields = [
        ('status_list', f_seq(f_msg(GoalStatus))),
    ]

    def __init__(self):
        self.status_list = []
