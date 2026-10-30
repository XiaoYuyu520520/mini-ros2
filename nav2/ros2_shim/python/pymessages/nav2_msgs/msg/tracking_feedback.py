from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped
from pymessages.std_msgs.msg.header import Header

class TrackingFeedback(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('position_tracking_error', f_scalar('f')),
        ('heading_tracking_error', f_scalar('f')),
        ('current_path_index', f_scalar('I')),
        ('robot_pose', f_msg(PoseStamped)),
        ('distance_to_goal', f_scalar('f')),
        ('speed', f_scalar('f')),
        ('remaining_path_length', f_scalar('f')),
    ]

    def __init__(self):
        self.header = Header()
        self.position_tracking_error = 0.0
        self.heading_tracking_error = 0.0
        self.current_path_index = 0
        self.robot_pose = PoseStamped()
        self.distance_to_goal = 0.0
        self.speed = 0.0
        self.remaining_path_length = 0.0
