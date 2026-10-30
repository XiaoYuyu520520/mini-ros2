from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class TF2Error(Msg):
    NO_ERROR = 0
    LOOKUP_ERROR = 1
    CONNECTIVITY_ERROR = 2
    EXTRAPOLATION_ERROR = 3
    INVALID_ARGUMENT_ERROR = 4
    TIMEOUT_ERROR = 5
    TRANSFORM_ERROR = 6
    _fields = [
        ('error', f_scalar('B')),
        ('error_string', f_string()),
    ]

    def __init__(self):
        self.error = 0
        self.error_string = ""
