from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)

class UndockRobot:

    class Goal(Msg):
        _fields = [
            ('dock_type', f_string()),
            ('max_undocking_time', f_scalar('f')),
        ]

        def __init__(self):
            self.dock_type = ""
            self.max_undocking_time = 0.0

    class Result(Msg):
        NONE = 0
        GOAL_REJECTED = 1
        SEND_GOAL_FAILURE = 2
        DOCK_NOT_VALID = 902
        FAILED_TO_CONTROL = 905
        TIMEOUT = 907
        UNKNOWN = 999
        _fields = [
            ('success', f_bool()),
            ('error_code', f_scalar('H')),
            ('error_msg', f_string()),
        ]

        def __init__(self):
            self.success = False
            self.error_code = 0
            self.error_msg = ""

    class Feedback(Msg):

        def __init__(self):
            pass

UndockRobot.Impl = build_action_impl(UndockRobot)
