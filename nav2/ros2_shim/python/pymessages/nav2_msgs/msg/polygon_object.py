from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.point32 import Point32
from pymessages.std_msgs.msg.header import Header
from pymessages.unique_identifier_msgs.msg.uuid import UUID

class PolygonObject(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('uuid', f_msg(UUID)),
        ('points', f_seq(f_msg(Point32))),
        ('closed', f_bool()),
        ('value', f_scalar('b')),
    ]

    def __init__(self):
        self.header = Header()
        self.uuid = UUID()
        self.points = []
        self.closed = False
        self.value = 0
