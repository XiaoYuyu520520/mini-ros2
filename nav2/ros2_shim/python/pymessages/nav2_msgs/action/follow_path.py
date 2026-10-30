from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)
from pymessages.nav2_msgs.msg.tracking_feedback import TrackingFeedback
from pymessages.nav_msgs.msg.path import Path
from pymessages.std_msgs.msg.empty import Empty

class FollowPath:

    class Goal(Msg):
        _fields = [
            ('path', f_msg(Path)),
            ('controller_id', f_string()),
            ('goal_checker_id', f_string()),
            ('progress_checker_id', f_string()),
            ('path_handler_id', f_string()),
        ]

        def __init__(self):
            self.path = Path()
            self.controller_id = ""
            self.goal_checker_id = ""
            self.progress_checker_id = ""
            self.path_handler_id = ""

    class Result(Msg):
        NONE = 0
        GOAL_REJECTED = 1
        SEND_GOAL_FAILURE = 2
        UNKNOWN = 100
        INVALID_CONTROLLER = 101
        TF_ERROR = 102
        INVALID_PATH = 103
        PATIENCE_EXCEEDED = 104
        FAILED_TO_MAKE_PROGRESS = 105
        NO_VALID_CONTROL = 106
        CONTROLLER_TIMED_OUT = 107
        TIMEOUT = 108
        _fields = [
            ('result', f_msg(Empty)),
            ('error_code', f_scalar('H')),
            ('error_msg', f_string()),
        ]

        def __init__(self):
            self.result = Empty()
            self.error_code = 0
            self.error_msg = ""

    class Feedback(Msg):
        _fields = [
            ('tracking_feedback', f_msg(TrackingFeedback)),
        ]

        def __init__(self):
            self.tracking_feedback = TrackingFeedback()

FollowPath.Impl = build_action_impl(FollowPath)
