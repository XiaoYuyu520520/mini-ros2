from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration
from pymessages.nav_msgs.msg.path import Path

class SmoothPath:

    class Goal(Msg):
        _fields = [
            ('path', f_msg(Path)),
            ('smoother_id', f_string()),
            ('max_smoothing_duration', f_msg(Duration)),
            ('check_for_collisions', f_bool()),
        ]

        def __init__(self):
            self.path = Path()
            self.smoother_id = ""
            self.max_smoothing_duration = Duration()
            self.check_for_collisions = False

    class Result(Msg):
        NONE = 0
        GOAL_REJECTED = 1
        SEND_GOAL_FAILURE = 2
        UNKNOWN = 500
        INVALID_SMOOTHER = 501
        TIMEOUT = 502
        SMOOTHED_PATH_IN_COLLISION = 503
        FAILED_TO_SMOOTH_PATH = 504
        INVALID_PATH = 505
        _fields = [
            ('path', f_msg(Path)),
            ('smoothing_duration', f_msg(Duration)),
            ('was_completed', f_bool()),
            ('error_code', f_scalar('H')),
            ('error_msg', f_string()),
        ]

        def __init__(self):
            self.path = Path()
            self.smoothing_duration = Duration()
            self.was_completed = False
            self.error_code = 0
            self.error_msg = ""

    class Feedback(Msg):

        def __init__(self):
            pass

SmoothPath.Impl = build_action_impl(SmoothPath)
