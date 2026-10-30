from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped
from pymessages.nav_msgs.msg.path import Path

class ComputePathToPose:

    class Goal(Msg):
        _fields = [
            ('goal', f_msg(PoseStamped)),
            ('start', f_msg(PoseStamped)),
            ('viapoints', f_seq(f_msg(PoseStamped))),
            ('planner_id', f_string()),
            ('use_start', f_bool()),
        ]

        def __init__(self):
            self.goal = PoseStamped()
            self.start = PoseStamped()
            self.viapoints = []
            self.planner_id = ""
            self.use_start = False

    class Result(Msg):
        NONE = 0
        GOAL_REJECTED = 1
        SEND_GOAL_FAILURE = 2
        UNKNOWN = 200
        INVALID_PLANNER = 201
        TF_ERROR = 202
        START_OUTSIDE_MAP = 203
        GOAL_OUTSIDE_MAP = 204
        START_OCCUPIED = 205
        GOAL_OCCUPIED = 206
        TIMEOUT = 207
        NO_VALID_PATH = 208
        _fields = [
            ('path', f_msg(Path)),
            ('planning_time', f_msg(Duration)),
            ('error_code', f_scalar('H')),
            ('error_msg', f_string()),
        ]

        def __init__(self):
            self.path = Path()
            self.planning_time = Duration()
            self.error_code = 0
            self.error_msg = ""

    class Feedback(Msg):

        def __init__(self):
            pass

ComputePathToPose.Impl = build_action_impl(ComputePathToPose)
