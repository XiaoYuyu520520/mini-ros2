from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.quaternion import Quaternion
from pymessages.geometry_msgs.msg.vector3 import Vector3
from pymessages.std_msgs.msg.header import Header

class Imu(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('orientation', f_msg(Quaternion)),
        ('orientation_covariance', f_array(f_scalar('d'), 9)),
        ('angular_velocity', f_msg(Vector3)),
        ('angular_velocity_covariance', f_array(f_scalar('d'), 9)),
        ('linear_acceleration', f_msg(Vector3)),
        ('linear_acceleration_covariance', f_array(f_scalar('d'), 9)),
    ]

    def __init__(self):
        self.header = Header()
        self.orientation = Quaternion()
        self.orientation_covariance = [0.0] * 9
        self.angular_velocity = Vector3()
        self.angular_velocity_covariance = [0.0] * 9
        self.linear_acceleration = Vector3()
        self.linear_acceleration_covariance = [0.0] * 9
