from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class NavSatStatus(Msg):
    STATUS_UNKNOWN = -2
    STATUS_NO_FIX = -1
    STATUS_FIX = 0
    STATUS_SBAS_FIX = 1
    STATUS_GBAS_FIX = 2
    SERVICE_UNKNOWN = 0
    SERVICE_GPS = 1
    SERVICE_GLONASS = 2
    SERVICE_COMPASS = 4
    SERVICE_GALILEO = 8
    _fields = [
        ('status', f_scalar('b')),
        ('service', f_scalar('H')),
    ]

    def __init__(self):
        self.status = 0
        self.service = 0
