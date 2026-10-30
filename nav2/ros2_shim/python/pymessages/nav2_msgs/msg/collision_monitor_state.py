from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class CollisionMonitorState(Msg):
    DO_NOTHING = 0
    STOP = 1
    SLOWDOWN = 2
    APPROACH = 3
    LIMIT = 4
    _fields = [
        ('action_type', f_scalar('B')),
        ('polygon_name', f_string()),
    ]

    def __init__(self):
        self.action_type = 0
        self.polygon_name = ""
