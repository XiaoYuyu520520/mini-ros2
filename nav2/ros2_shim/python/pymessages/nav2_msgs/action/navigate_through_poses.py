from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped
from pymessages.nav2_msgs.msg.waypoint_status import WaypointStatus
from pymessages.nav_msgs.msg.goals import Goals

class NavigateThroughPoses:

    class Goal(Msg):
        _fields = [
            ('poses', f_msg(Goals)),
            ('behavior_tree', f_string()),
        ]

        def __init__(self):
            self.poses = Goals()
            self.behavior_tree = ""

    class Result(Msg):
        NONE = 0
        GOAL_REJECTED = 1
        SEND_GOAL_FAILURE = 2
        UNKNOWN = 9100
        FAILED_TO_LOAD_BEHAVIOR_TREE = 9101
        TF_ERROR = 9102
        TIMEOUT = 9103
        _fields = [
            ('error_code', f_scalar('H')),
            ('error_msg', f_string()),
            ('waypoint_statuses', f_seq(f_msg(WaypointStatus))),
        ]

        def __init__(self):
            self.error_code = 0
            self.error_msg = ""
            self.waypoint_statuses = []

    class Feedback(Msg):
        _fields = [
            ('current_pose', f_msg(PoseStamped)),
            ('navigation_time', f_msg(Duration)),
            ('estimated_time_remaining', f_msg(Duration)),
            ('number_of_recoveries', f_scalar('h')),
            ('distance_remaining', f_scalar('f')),
            ('position_tracking_error', f_scalar('f')),
            ('heading_tracking_error', f_scalar('f')),
            ('number_of_poses_remaining', f_scalar('h')),
            ('waypoint_statuses', f_seq(f_msg(WaypointStatus))),
        ]

        def __init__(self):
            self.current_pose = PoseStamped()
            self.navigation_time = Duration()
            self.estimated_time_remaining = Duration()
            self.number_of_recoveries = 0
            self.distance_remaining = 0.0
            self.position_tracking_error = 0.0
            self.heading_tracking_error = 0.0
            self.number_of_poses_remaining = 0
            self.waypoint_statuses = []

NavigateThroughPoses.Impl = build_action_impl(NavigateThroughPoses)
