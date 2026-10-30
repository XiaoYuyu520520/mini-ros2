from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped

class NavigateToPose:

    class Goal(Msg):
        _fields = [
            ('pose', f_msg(PoseStamped)),
            ('behavior_tree', f_string()),
        ]

        def __init__(self):
            self.pose = PoseStamped()
            self.behavior_tree = ""

    class Result(Msg):
        NONE = 0
        GOAL_REJECTED = 1
        SEND_GOAL_FAILURE = 2
        UNKNOWN = 9000
        FAILED_TO_LOAD_BEHAVIOR_TREE = 9001
        TF_ERROR = 9002
        TIMEOUT = 9003
        _fields = [
            ('error_code', f_scalar('H')),
            ('error_msg', f_string()),
        ]

        def __init__(self):
            self.error_code = 0
            self.error_msg = ""

    class Feedback(Msg):
        _fields = [
            ('current_pose', f_msg(PoseStamped)),
            ('navigation_time', f_msg(Duration)),
            ('estimated_time_remaining', f_msg(Duration)),
            ('number_of_recoveries', f_scalar('h')),
            ('distance_remaining', f_scalar('f')),
            ('position_tracking_error', f_scalar('f')),
            ('heading_tracking_error', f_scalar('f')),
        ]

        def __init__(self):
            self.current_pose = PoseStamped()
            self.navigation_time = Duration()
            self.estimated_time_remaining = Duration()
            self.number_of_recoveries = 0
            self.distance_remaining = 0.0
            self.position_tracking_error = 0.0
            self.heading_tracking_error = 0.0

NavigateToPose.Impl = build_action_impl(NavigateToPose)
