from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class MeshFile(Msg):
    _fields = [
        ('filename', f_string()),
        ('data', f_seq(f_scalar('B'))),
    ]

    def __init__(self):
        self.filename = ""
        self.data = []
