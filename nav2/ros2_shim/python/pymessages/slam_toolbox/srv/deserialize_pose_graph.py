from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.pose2_d import Pose2D

class DeserializePoseGraph:

    class Request(Msg):
        UNSET = 0
        START_AT_FIRST_NODE = 1
        START_AT_GIVEN_POSE = 2
        LOCALIZE_AT_POSE = 3
        _fields = [
            ('filename', f_string()),
            ('match_type', f_scalar('b')),
            ('initial_pose', f_msg(Pose2D)),
        ]

        def __init__(self):
            self.filename = ""
            self.match_type = 0
            self.initial_pose = Pose2D()

    class Response(Msg):

        def __init__(self):
            pass
