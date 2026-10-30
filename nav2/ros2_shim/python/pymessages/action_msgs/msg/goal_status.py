from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.action_msgs.msg.goal_info import GoalInfo

class GoalStatus(Msg):
    STATUS_UNKNOWN = 0
    STATUS_ACCEPTED = 1
    STATUS_EXECUTING = 2
    STATUS_CANCELING = 3
    STATUS_SUCCEEDED = 4
    STATUS_CANCELED = 5
    STATUS_ABORTED = 6
    _fields = [
        ('goal_info', f_msg(GoalInfo)),
        ('status', f_scalar('b')),
    ]

    def __init__(self):
        self.goal_info = GoalInfo()
        self.status = 0
