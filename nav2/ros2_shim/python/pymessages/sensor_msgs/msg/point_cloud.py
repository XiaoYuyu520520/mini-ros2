from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.point32 import Point32
from pymessages.sensor_msgs.msg.channel_float32 import ChannelFloat32
from pymessages.std_msgs.msg.header import Header

class PointCloud(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('points', f_seq(f_msg(Point32))),
        ('channels', f_seq(f_msg(ChannelFloat32))),
    ]

    def __init__(self):
        self.header = Header()
        self.points = []
        self.channels = []
