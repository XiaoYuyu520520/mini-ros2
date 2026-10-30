from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class GeoPoint(Msg):
    _fields = [
        ('latitude', f_scalar('d')),
        ('longitude', f_scalar('d')),
        ('altitude', f_scalar('d')),
    ]

    def __init__(self):
        self.latitude = 0.0
        self.longitude = 0.0
        self.altitude = 0.0
