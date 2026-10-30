from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.point32 import Point32
from pymessages.geometry_msgs.msg.vector3 import Vector3
from pymessages.std_msgs.msg.header import Header

class VoxelGrid(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('data', f_seq(f_scalar('I'))),
        ('origin', f_msg(Point32)),
        ('resolutions', f_msg(Vector3)),
        ('size_x', f_scalar('I')),
        ('size_y', f_scalar('I')),
        ('size_z', f_scalar('I')),
    ]

    def __init__(self):
        self.header = Header()
        self.data = []
        self.origin = Point32()
        self.resolutions = Vector3()
        self.size_x = 0
        self.size_y = 0
        self.size_z = 0
