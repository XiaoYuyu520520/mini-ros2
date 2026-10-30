from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped
from pymessages.nav2_msgs.msg.waypoint_status import WaypointStatus

class FollowWaypoints:

    class Goal(Msg):
        _fields = [
            ('number_of_loops', f_scalar('I')),
            ('goal_index', f_scalar('I')),
            ('poses', f_seq(f_msg(PoseStamped))),
        ]

        def __init__(self):
            self.number_of_loops = 0
            self.goal_index = 0
            self.poses = []

    class Result(Msg):
        NONE = 0
        UNKNOWN = 600
        TASK_EXECUTOR_FAILED = 601
        NO_VALID_WAYPOINTS = 602
        STOP_ON_MISSED_WAYPOINT = 603
        _fields = [
            ('missed_waypoints', f_seq(f_msg(WaypointStatus))),
            ('error_code', f_scalar('H')),
            ('error_msg', f_string()),
        ]

        def __init__(self):
            self.missed_waypoints = []
            self.error_code = 0
            self.error_msg = ""

    class Feedback(Msg):
        _fields = [
            ('current_waypoint', f_scalar('I')),
        ]

        def __init__(self):
            self.current_waypoint = 0

FollowWaypoints.Impl = build_action_impl(FollowWaypoints)
