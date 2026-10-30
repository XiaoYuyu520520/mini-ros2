from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class UUID(Msg):
    _fields = [
        ('uuid', f_array(f_scalar('B'), 16)),
    ]

    def __init__(self):
        self.uuid = [0] * 16
