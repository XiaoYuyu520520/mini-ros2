from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.point import Point
from pymessages.std_msgs.msg.header import Header

class GridCells(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('cell_width', f_scalar('f')),
        ('cell_height', f_scalar('f')),
        ('cells', f_seq(f_msg(Point))),
    ]

    def __init__(self):
        self.header = Header()
        self.cell_width = 0.0
        self.cell_height = 0.0
        self.cells = []
