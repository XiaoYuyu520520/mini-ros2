from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration
from pymessages.geometry_msgs.msg.point import Point
from pymessages.std_msgs.msg.color_rgba import ColorRGBA
from pymessages.std_msgs.msg.header import Header

class ImageMarker(Msg):
    CIRCLE = 0
    LINE_STRIP = 1
    LINE_LIST = 2
    POLYGON = 3
    POINTS = 4
    ADD = 0
    REMOVE = 1
    _fields = [
        ('header', f_msg(Header)),
        ('ns', f_string()),
        ('id', f_scalar('i')),
        ('type', f_scalar('i')),
        ('action', f_scalar('i')),
        ('position', f_msg(Point)),
        ('scale', f_scalar('f')),
        ('outline_color', f_msg(ColorRGBA)),
        ('filled', f_scalar('B')),
        ('fill_color', f_msg(ColorRGBA)),
        ('lifetime', f_msg(Duration)),
        ('points', f_seq(f_msg(Point))),
        ('outline_colors', f_seq(f_msg(ColorRGBA))),
    ]

    def __init__(self):
        self.header = Header()
        self.ns = ""
        self.id = 0
        self.type = 0
        self.action = 0
        self.position = Point()
        self.scale = 0.0
        self.outline_color = ColorRGBA()
        self.filled = 0
        self.fill_color = ColorRGBA()
        self.lifetime = Duration()
        self.points = []
        self.outline_colors = []
