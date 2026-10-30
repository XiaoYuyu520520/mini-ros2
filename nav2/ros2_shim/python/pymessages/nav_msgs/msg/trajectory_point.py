from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.accel import Accel
from pymessages.geometry_msgs.msg.pose import Pose
from pymessages.geometry_msgs.msg.twist import Twist
from pymessages.geometry_msgs.msg.wrench import Wrench
from pymessages.std_msgs.msg.header import Header

class TrajectoryPoint(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('pose', f_msg(Pose)),
        ('velocity', f_msg(Twist)),
        ('acceleration', f_msg(Accel)),
        ('effort', f_msg(Wrench)),
    ]

    def __init__(self):
        self.header = Header()
        self.pose = Pose()
        self.velocity = Twist()
        self.acceleration = Accel()
        self.effort = Wrench()
