from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class CollisionDetectorState(Msg):
    _fields = [
        ('polygons', f_seq(f_string())),
        ('detections', f_seq(f_bool())),
    ]

    def __init__(self):
        self.polygons = []
        self.detections = []
