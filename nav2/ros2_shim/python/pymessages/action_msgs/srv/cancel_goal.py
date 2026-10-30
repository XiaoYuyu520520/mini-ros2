from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.action_msgs.msg.goal_info import GoalInfo

class CancelGoal:

    class Request(Msg):
        _fields = [
            ('goal_info', f_msg(GoalInfo)),
        ]

        def __init__(self):
            self.goal_info = GoalInfo()

    class Response(Msg):
        ERROR_NONE = 0
        ERROR_REJECTED = 1
        ERROR_UNKNOWN_GOAL_ID = 2
        ERROR_GOAL_TERMINATED = 3
        _fields = [
            ('return_code', f_scalar('b')),
            ('goals_canceling', f_seq(f_msg(GoalInfo))),
        ]

        def __init__(self):
            self.return_code = 0
            self.goals_canceling = []
