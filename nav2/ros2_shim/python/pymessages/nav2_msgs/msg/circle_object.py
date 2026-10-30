from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.point32 import Point32
from pymessages.std_msgs.msg.header import Header
from pymessages.unique_identifier_msgs.msg.uuid import UUID

class CircleObject(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('uuid', f_msg(UUID)),
        ('center', f_msg(Point32)),
        ('radius', f_scalar('f')),
        ('fill', f_bool()),
        ('value', f_scalar('b')),
    ]

    def __init__(self):
        self.header = Header()
        self.uuid = UUID()
        self.center = Point32()
        self.radius = 0.0
        self.fill = False
        self.value = 0
