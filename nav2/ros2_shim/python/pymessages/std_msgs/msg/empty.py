from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class Empty(Msg):

    def __init__(self):
        pass
