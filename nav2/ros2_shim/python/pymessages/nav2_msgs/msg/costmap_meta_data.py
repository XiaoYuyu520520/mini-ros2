from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.time import Time
from pymessages.geometry_msgs.msg.pose import Pose

class CostmapMetaData(Msg):
    _fields = [
        ('map_load_time', f_msg(Time)),
        ('update_time', f_msg(Time)),
        ('layer', f_string()),
        ('resolution', f_scalar('f')),
        ('size_x', f_scalar('I')),
        ('size_y', f_scalar('I')),
        ('origin', f_msg(Pose)),
    ]

    def __init__(self):
        self.map_load_time = Time()
        self.update_time = Time()
        self.layer = ""
        self.resolution = 0.0
        self.size_x = 0
        self.size_y = 0
        self.origin = Pose()
