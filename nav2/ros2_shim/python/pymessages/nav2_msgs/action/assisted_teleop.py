from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration

class AssistedTeleop:

    class Goal(Msg):
        _fields = [
            ('time_allowance', f_msg(Duration)),
        ]

        def __init__(self):
            self.time_allowance = Duration()

    class Result(Msg):
        NONE = 0
        GOAL_REJECTED = 1
        SEND_GOAL_FAILURE = 2
        UNKNOWN = 730
        TIMEOUT = 731
        TF_ERROR = 732
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
            ('current_teleop_duration', f_msg(Duration)),
        ]

        def __init__(self):
            self.current_teleop_duration = Duration()

AssistedTeleop.Impl = build_action_impl(AssistedTeleop)
