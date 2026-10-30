from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.geo_pose import GeoPose
from pymessages.std_msgs.msg.header import Header

class GeoPoseStamped(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('pose', f_msg(GeoPose)),
    ]

    def __init__(self):
        self.header = Header()
        self.pose = GeoPose()
