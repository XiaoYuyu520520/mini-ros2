from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav_msgs.msg.path import Path

class IsPathValid:

    class Request(Msg):
        _fields = [
            ('path', f_msg(Path)),
            ('max_cost', f_scalar('B')),
            ('consider_unknown_as_obstacle', f_bool()),
            ('layer_name', f_string()),
            ('footprint', f_string()),
            ('stop_at_first_collision', f_bool()),
            ('max_lookahead_distance', f_scalar('d')),
        ]

        def __init__(self):
            self.path = Path()
            self.max_cost = 0
            self.consider_unknown_as_obstacle = False
            self.layer_name = ""
            self.footprint = ""
            self.stop_at_first_collision = False
            self.max_lookahead_distance = 0.0

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
            ('is_valid', f_bool()),
            ('invalid_pose_indices', f_seq(f_scalar('i'))),
        ]

        def __init__(self):
            self.success = False
            self.is_valid = False
            self.invalid_pose_indices = []
