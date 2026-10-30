from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.point import Point
from pymessages.shape_msgs.msg.mesh_triangle import MeshTriangle

class Mesh(Msg):
    _fields = [
        ('triangles', f_seq(f_msg(MeshTriangle))),
        ('vertices', f_seq(f_msg(Point))),
    ]

    def __init__(self):
        self.triangles = []
        self.vertices = []
