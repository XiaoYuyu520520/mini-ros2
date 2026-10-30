from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class ProjectedMapInfo(Msg):
    _fields = [
        ('frame_id', f_string()),
        ('x', f_scalar('d')),
        ('y', f_scalar('d')),
        ('width', f_scalar('d')),
        ('height', f_scalar('d')),
        ('min_z', f_scalar('d')),
        ('max_z', f_scalar('d')),
    ]

    def __init__(self):
        self.frame_id = ""
        self.x = 0.0
        self.y = 0.0
        self.width = 0.0
        self.height = 0.0
        self.min_z = 0.0
        self.max_z = 0.0
