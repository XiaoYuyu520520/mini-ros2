from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.header import Header

class JointState(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('name', f_seq(f_string())),
        ('position', f_seq(f_scalar('d'))),
        ('velocity', f_seq(f_scalar('d'))),
        ('effort', f_seq(f_scalar('d'))),
    ]

    def __init__(self):
        self.header = Header()
        self.name = []
        self.position = []
        self.velocity = []
        self.effort = []
