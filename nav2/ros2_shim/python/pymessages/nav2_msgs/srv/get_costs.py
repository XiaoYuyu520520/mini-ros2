from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.pose_stamped import PoseStamped

class GetCosts:

    class Request(Msg):
        _fields = [
            ('use_footprint', f_bool()),
            ('poses', f_seq(f_msg(PoseStamped))),
        ]

        def __init__(self):
            self.use_footprint = False
            self.poses = []

    class Response(Msg):
        _fields = [
            ('costs', f_seq(f_scalar('f'))),
            ('success', f_bool()),
        ]

        def __init__(self):
            self.costs = []
            self.success = False
