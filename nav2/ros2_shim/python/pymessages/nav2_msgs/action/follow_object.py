from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration

class FollowObject:

    class Goal(Msg):
        _fields = [
            ('pose_topic', f_string()),
            ('tracked_frame', f_string()),
            ('max_duration', f_msg(Duration)),
        ]

        def __init__(self):
            self.pose_topic = ""
            self.tracked_frame = ""
            self.max_duration = Duration()

    class Result(Msg):
        NONE = 0
        GOAL_REJECTED = 1
        SEND_GOAL_FAILURE = 2
        TF_ERROR = 901
        FAILED_TO_DETECT_OBJECT = 902
        FAILED_TO_CONTROL = 903
        TIMEOUT = 904
        UNKNOWN = 999
        _fields = [
            ('total_elapsed_time', f_msg(Duration)),
            ('error_code', f_scalar('H')),
            ('num_retries', f_scalar('H')),
            ('error_msg', f_string()),
        ]

        def __init__(self):
            self.total_elapsed_time = Duration()
            self.error_code = 0
            self.num_retries = 0
            self.error_msg = ""

    class Feedback(Msg):
        NONE = 0
        INITIAL_PERCEPTION = 1
        CONTROLLING = 2
        STOPPING = 3
        RETRY = 4
        _fields = [
            ('state', f_scalar('H')),
            ('following_time', f_msg(Duration)),
            ('num_retries', f_scalar('H')),
        ]

        def __init__(self):
            self.state = 0
            self.following_time = Duration()
            self.num_retries = 0

FollowObject.Impl = build_action_impl(FollowObject)
