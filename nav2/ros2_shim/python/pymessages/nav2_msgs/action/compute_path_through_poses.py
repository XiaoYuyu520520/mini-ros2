from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped
from pymessages.nav_msgs.msg.goals import Goals
from pymessages.nav_msgs.msg.path import Path

class ComputePathThroughPoses:

    class Goal(Msg):
        _fields = [
            ('goals', f_msg(Goals)),
            ('start', f_msg(PoseStamped)),
            ('planner_id', f_string()),
            ('use_start', f_bool()),
        ]

        def __init__(self):
            self.goals = Goals()
            self.start = PoseStamped()
            self.planner_id = ""
            self.use_start = False

    class Result(Msg):
        ALL_GOALS = -1
        NONE = 0
        GOAL_REJECTED = 1
        SEND_GOAL_FAILURE = 2
        UNKNOWN = 300
        INVALID_PLANNER = 301
        TF_ERROR = 302
        START_OUTSIDE_MAP = 303
        GOAL_OUTSIDE_MAP = 304
        START_OCCUPIED = 305
        GOAL_OCCUPIED = 306
        TIMEOUT = 307
        NO_VALID_PATH = 308
        NO_VIAPOINTS_GIVEN = 309
        _fields = [
            ('path', f_msg(Path)),
            ('last_reached_index', f_scalar('h')),
            ('planning_time', f_msg(Duration)),
            ('error_code', f_scalar('H')),
            ('error_msg', f_string()),
        ]

        def __init__(self):
            self.path = Path()
            self.last_reached_index = 0
            self.planning_time = Duration()
            self.error_code = 0
            self.error_msg = ""

    class Feedback(Msg):

        def __init__(self):
            pass

ComputePathThroughPoses.Impl = build_action_impl(ComputePathThroughPoses)
