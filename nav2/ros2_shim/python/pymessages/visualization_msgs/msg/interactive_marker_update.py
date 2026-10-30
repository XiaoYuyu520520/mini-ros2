from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.visualization_msgs.msg.interactive_marker import InteractiveMarker
from pymessages.visualization_msgs.msg.interactive_marker_pose import InteractiveMarkerPose

class InteractiveMarkerUpdate(Msg):
    KEEP_ALIVE = 0
    UPDATE = 1
    _fields = [
        ('server_id', f_string()),
        ('seq_num', f_scalar('Q')),
        ('type', f_scalar('B')),
        ('markers', f_seq(f_msg(InteractiveMarker))),
        ('poses', f_seq(f_msg(InteractiveMarkerPose))),
        ('erases', f_seq(f_string())),
    ]

    def __init__(self):
        self.server_id = ""
        self.seq_num = 0
        self.type = 0
        self.markers = []
        self.poses = []
        self.erases = []
