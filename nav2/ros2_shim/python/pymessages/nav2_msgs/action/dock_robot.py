from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped

class DockRobot:

    class Goal(Msg):
        _fields = [
            ('use_dock_id', f_bool()),
            ('dock_id', f_string()),
            ('dock_pose', f_msg(PoseStamped)),
            ('dock_type', f_string()),
            ('max_staging_time', f_scalar('f')),
            ('navigate_to_staging_pose', f_bool()),
        ]

        def __init__(self):
            self.use_dock_id = False
            self.dock_id = ""
            self.dock_pose = PoseStamped()
            self.dock_type = ""
            self.max_staging_time = 0.0
            self.navigate_to_staging_pose = False

    class Result(Msg):
        NONE = 0
        GOAL_REJECTED = 1
        SEND_GOAL_FAILURE = 2
        DOCK_NOT_IN_DB = 901
        DOCK_NOT_VALID = 902
        FAILED_TO_STAGE = 903
        FAILED_TO_DETECT_DOCK = 904
        FAILED_TO_CONTROL = 905
        FAILED_TO_CHARGE = 906
        TIMEOUT = 907
        UNKNOWN = 999
        _fields = [
            ('success', f_bool()),
            ('error_code', f_scalar('H')),
            ('num_retries', f_scalar('H')),
            ('error_msg', f_string()),
        ]

        def __init__(self):
            self.success = False
            self.error_code = 0
            self.num_retries = 0
            self.error_msg = ""

    class Feedback(Msg):
        NONE = 0
        NAV_TO_STAGING_POSE = 1
        INITIAL_PERCEPTION = 2
        CONTROLLING = 3
        WAIT_FOR_CHARGE = 4
        RETRY = 5
        _fields = [
            ('state', f_scalar('H')),
            ('docking_time', f_msg(Duration)),
            ('num_retries', f_scalar('H')),
        ]

        def __init__(self):
            self.state = 0
            self.docking_time = Duration()
            self.num_retries = 0

DockRobot.Impl = build_action_impl(DockRobot)
