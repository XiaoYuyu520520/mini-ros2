from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.geo_pose_stamped import GeoPoseStamped
from pymessages.std_msgs.msg.header import Header

class GeoPath(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('poses', f_seq(f_msg(GeoPoseStamped))),
    ]

    def __init__(self):
        self.header = Header()
        self.poses = []
