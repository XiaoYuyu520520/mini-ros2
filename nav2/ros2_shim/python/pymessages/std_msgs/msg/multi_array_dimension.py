from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class MultiArrayDimension(Msg):
    _fields = [
        ('label', f_string()),
        ('size', f_scalar('I')),
        ('stride', f_scalar('I')),
    ]

    def __init__(self):
        self.label = ""
        self.size = 0
        self.stride = 0
