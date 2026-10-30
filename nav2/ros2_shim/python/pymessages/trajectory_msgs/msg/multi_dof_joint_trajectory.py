from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.header import Header
from pymessages.trajectory_msgs.msg.multi_dof_joint_trajectory_point import MultiDOFJointTrajectoryPoint

class MultiDOFJointTrajectory(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('joint_names', f_seq(f_string())),
        ('points', f_seq(f_msg(MultiDOFJointTrajectoryPoint))),
    ]

    def __init__(self):
        self.header = Header()
        self.joint_names = []
        self.points = []
