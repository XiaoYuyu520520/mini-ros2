from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.sensor_msgs.msg.point_field import PointField
from pymessages.std_msgs.msg.header import Header

class PointCloud2(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('height', f_scalar('I')),
        ('width', f_scalar('I')),
        ('fields', f_seq(f_msg(PointField))),
        ('is_bigendian', f_bool()),
        ('point_step', f_scalar('I')),
        ('row_step', f_scalar('I')),
        ('data', f_seq(f_scalar('B'))),
        ('is_dense', f_bool()),
    ]

    def __init__(self):
        self.header = Header()
        self.height = 0
        self.width = 0
        self.fields = []
        self.is_bigendian = False
        self.point_step = 0
        self.row_step = 0
        self.data = []
        self.is_dense = False
