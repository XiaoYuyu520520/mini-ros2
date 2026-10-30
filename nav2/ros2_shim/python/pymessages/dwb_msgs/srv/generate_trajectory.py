from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.dwb_msgs.msg.trajectory2_d import Trajectory2D
from pymessages.geometry_msgs.msg.pose import Pose
from pymessages.nav_2d_msgs.msg.twist2_d import Twist2D

class GenerateTrajectory:

    class Request(Msg):
        _fields = [
            ('start_pose', f_msg(Pose)),
            ('start_vel', f_msg(Twist2D)),
            ('cmd_vel', f_msg(Twist2D)),
        ]

        def __init__(self):
            self.start_pose = Pose()
            self.start_vel = Twist2D()
            self.cmd_vel = Twist2D()

    class Response(Msg):
        _fields = [
            ('traj', f_msg(Trajectory2D)),
        ]

        def __init__(self):
            self.traj = Trajectory2D()
