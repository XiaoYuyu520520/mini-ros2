from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav_2d_msgs.msg.twist2_d import Twist2D

class GenerateTwists:

    class Request(Msg):
        _fields = [
            ('current_vel', f_msg(Twist2D)),
        ]

        def __init__(self):
            self.current_vel = Twist2D()

    class Response(Msg):
        _fields = [
            ('twists', f_seq(f_msg(Twist2D))),
        ]

        def __init__(self):
            self.twists = []
