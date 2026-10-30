from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped

class WaypointStatus(Msg):
    PENDING = 0
    COMPLETED = 1
    SKIPPED = 2
    FAILED = 3
    _fields = [
        ('waypoint_status', f_scalar('B')),
        ('waypoint_index', f_scalar('I')),
        ('waypoint_pose', f_msg(PoseStamped)),
        ('error_code', f_scalar('H')),
        ('error_msg', f_string()),
    ]

    def __init__(self):
        self.waypoint_status = 0
        self.waypoint_index = 0
        self.waypoint_pose = PoseStamped()
        self.error_code = 0
        self.error_msg = ""
