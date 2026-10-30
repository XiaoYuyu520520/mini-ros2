from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.sensor_msgs.msg.nav_sat_status import NavSatStatus
from pymessages.std_msgs.msg.header import Header

class NavSatFix(Msg):
    COVARIANCE_TYPE_UNKNOWN = 0
    COVARIANCE_TYPE_APPROXIMATED = 1
    COVARIANCE_TYPE_DIAGONAL_KNOWN = 2
    COVARIANCE_TYPE_KNOWN = 3
    _fields = [
        ('header', f_msg(Header)),
        ('status', f_msg(NavSatStatus)),
        ('latitude', f_scalar('d')),
        ('longitude', f_scalar('d')),
        ('altitude', f_scalar('d')),
        ('position_covariance', f_array(f_scalar('d'), 9)),
        ('position_covariance_type', f_scalar('B')),
    ]

    def __init__(self):
        self.header = Header()
        self.status = NavSatStatus()
        self.latitude = 0.0
        self.longitude = 0.0
        self.altitude = 0.0
        self.position_covariance = [0.0] * 9
        self.position_covariance_type = 0
