from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped
from pymessages.nav2_msgs.msg.route import Route
from pymessages.nav_msgs.msg.path import Path

class ComputeRoute:

    class Goal(Msg):
        _fields = [
            ('start_id', f_scalar('H')),
            ('start', f_msg(PoseStamped)),
            ('goal_id', f_scalar('H')),
            ('goal', f_msg(PoseStamped)),
            ('use_start', f_bool()),
            ('use_poses', f_bool()),
        ]

        def __init__(self):
            self.start_id = 0
            self.start = PoseStamped()
            self.goal_id = 0
            self.goal = PoseStamped()
            self.use_start = False
            self.use_poses = False

    class Result(Msg):
        NONE = 0
        GOAL_REJECTED = 1
        SEND_GOAL_FAILURE = 2
        UNKNOWN = 400
        TF_ERROR = 401
        NO_VALID_GRAPH = 402
        INDETERMINANT_NODES_ON_GRAPH = 403
        TIMEOUT = 404
        NO_VALID_ROUTE = 405
        INVALID_EDGE_SCORER_USE = 407
        _fields = [
            ('planning_time', f_msg(Duration)),
            ('path', f_msg(Path)),
            ('route', f_msg(Route)),
            ('error_code', f_scalar('H')),
            ('error_msg', f_string()),
        ]

        def __init__(self):
            self.planning_time = Duration()
            self.path = Path()
            self.route = Route()
            self.error_code = 0
            self.error_msg = ""

    class Feedback(Msg):

        def __init__(self):
            pass

ComputeRoute.Impl = build_action_impl(ComputeRoute)
