from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.multi_array_dimension import MultiArrayDimension

class MultiArrayLayout(Msg):
    _fields = [
        ('dim', f_seq(f_msg(MultiArrayDimension))),
        ('data_offset', f_scalar('I')),
    ]

    def __init__(self):
        self.dim = []
        self.data_offset = 0
