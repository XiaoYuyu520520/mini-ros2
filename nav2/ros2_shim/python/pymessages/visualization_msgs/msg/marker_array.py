from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.visualization_msgs.msg.marker import Marker

class MarkerArray(Msg):
    _fields = [
        ('markers', f_seq(f_msg(Marker))),
    ]

    def __init__(self):
        self.markers = []
