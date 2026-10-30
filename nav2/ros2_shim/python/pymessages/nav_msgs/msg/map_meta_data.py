from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.time import Time
from pymessages.geometry_msgs.msg.pose import Pose

class MapMetaData(Msg):
    _fields = [
        ('map_load_time', f_msg(Time)),
        ('resolution', f_scalar('f')),
        ('width', f_scalar('I')),
        ('height', f_scalar('I')),
        ('origin', f_msg(Pose)),
    ]

    def __init__(self):
        self.map_load_time = Time()
        self.resolution = 0.0
        self.width = 0
        self.height = 0
        self.origin = Pose()
