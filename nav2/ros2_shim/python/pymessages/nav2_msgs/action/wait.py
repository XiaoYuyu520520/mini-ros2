from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration

class Wait:

    class Goal(Msg):
        _fields = [
            ('time', f_msg(Duration)),
        ]

        def __init__(self):
            self.time = Duration()

    class Result(Msg):
        NONE = 0
        GOAL_REJECTED = 1
        SEND_GOAL_FAILURE = 2
        UNKNOWN = 740
        TIMEOUT = 741
        _fields = [
            ('total_elapsed_time', f_msg(Duration)),
            ('error_code', f_scalar('H')),
            ('error_msg', f_string()),
        ]

        def __init__(self):
            self.total_elapsed_time = Duration()
            self.error_code = 0
            self.error_msg = ""

    class Feedback(Msg):
        _fields = [
            ('time_left', f_msg(Duration)),
        ]

        def __init__(self):
            self.time_left = Duration()

Wait.Impl = build_action_impl(Wait)
