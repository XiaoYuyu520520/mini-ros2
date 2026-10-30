from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.polygon import Polygon

class SolidPrimitive(Msg):
    BOX = 1
    SPHERE = 2
    CYLINDER = 3
    CONE = 4
    PRISM = 5
    BOX_X = 0
    BOX_Y = 1
    BOX_Z = 2
    SPHERE_RADIUS = 0
    CYLINDER_HEIGHT = 0
    CYLINDER_RADIUS = 1
    CONE_HEIGHT = 0
    CONE_RADIUS = 1
    PRISM_HEIGHT = 0
    _fields = [
        ('type', f_scalar('B')),
        ('dimensions', f_seq(f_scalar('d'))),
        ('polygon', f_msg(Polygon)),
    ]

    def __init__(self):
        self.type = 0
        self.dimensions = []
        self.polygon = Polygon()
