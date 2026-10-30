from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.sensor_msgs.msg.point_cloud2 import PointCloud2
from pymessages.std_msgs.msg.header import Header

class PointCloud2Update(Msg):
    ADD = 0
    DELETE = 1
    _fields = [
        ('header', f_msg(Header)),
        ('type', f_scalar('I')),
        ('points', f_msg(PointCloud2)),
    ]

    def __init__(self):
        self.header = Header()
        self.type = 0
        self.points = PointCloud2()
