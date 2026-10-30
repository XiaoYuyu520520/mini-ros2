from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.transform import Transform
from pymessages.geometry_msgs.msg.twist import Twist
from pymessages.geometry_msgs.msg.wrench import Wrench
from pymessages.std_msgs.msg.header import Header

class MultiDOFJointState(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('joint_names', f_seq(f_string())),
        ('transforms', f_seq(f_msg(Transform))),
        ('twist', f_seq(f_msg(Twist))),
        ('wrench', f_seq(f_msg(Wrench))),
    ]

    def __init__(self):
        self.header = Header()
        self.joint_names = []
        self.transforms = []
        self.twist = []
        self.wrench = []
