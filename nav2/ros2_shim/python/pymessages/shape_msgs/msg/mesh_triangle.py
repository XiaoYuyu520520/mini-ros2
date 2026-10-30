from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class MeshTriangle(Msg):
    _fields = [
        ('vertex_indices', f_array(f_scalar('I'), 3)),
    ]

    def __init__(self):
        self.vertex_indices = [0] * 3
