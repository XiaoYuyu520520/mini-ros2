from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.geo_pose import GeoPose

class GeoPoseWithCovariance(Msg):
    _fields = [
        ('pose', f_msg(GeoPose)),
        ('covariance', f_array(f_scalar('d'), 36)),
    ]

    def __init__(self):
        self.pose = GeoPose()
        self.covariance = [0.0] * 36
