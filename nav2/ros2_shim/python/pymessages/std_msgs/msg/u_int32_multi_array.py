from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.multi_array_layout import MultiArrayLayout

class UInt32MultiArray(Msg):
    _fields = [
        ('layout', f_msg(MultiArrayLayout)),
        ('data', f_seq(f_scalar('I'))),
    ]

    def __init__(self):
        self.layout = MultiArrayLayout()
        self.data = []
