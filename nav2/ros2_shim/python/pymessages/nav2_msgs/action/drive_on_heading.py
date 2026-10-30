from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration
from pymessages.geometry_msgs.msg.point import Point

class DriveOnHeading:

    class Goal(Msg):
        _fields = [
            ('target', f_msg(Point)),
            ('speed', f_scalar('f')),
            ('time_allowance', f_msg(Duration)),
            ('disable_collision_checks', f_bool()),
        ]

        def __init__(self):
            self.target = Point()
            self.speed = 0.0
            self.time_allowance = Duration()
            self.disable_collision_checks = False

    class Result(Msg):
        NONE = 0
        GOAL_REJECTED = 1
        SEND_GOAL_FAILURE = 2
        UNKNOWN = 720
        TIMEOUT = 721
        TF_ERROR = 722
        COLLISION_AHEAD = 723
        INVALID_INPUT = 724
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
            ('distance_traveled', f_scalar('f')),
        ]

        def __init__(self):
            self.distance_traveled = 0.0

DriveOnHeading.Impl = build_action_impl(DriveOnHeading)
