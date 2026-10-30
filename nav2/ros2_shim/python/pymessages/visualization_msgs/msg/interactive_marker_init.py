from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.visualization_msgs.msg.interactive_marker import InteractiveMarker

class InteractiveMarkerInit(Msg):
    _fields = [
        ('server_id', f_string()),
        ('seq_num', f_scalar('Q')),
        ('markers', f_seq(f_msg(InteractiveMarker))),
    ]

    def __init__(self):
        self.server_id = ""
        self.seq_num = 0
        self.markers = []
