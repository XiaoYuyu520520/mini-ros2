from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration
from pymessages.geometry_msgs.msg.point import Point
from pymessages.geometry_msgs.msg.pose import Pose
from pymessages.geometry_msgs.msg.vector3 import Vector3
from pymessages.sensor_msgs.msg.compressed_image import CompressedImage
from pymessages.std_msgs.msg.color_rgba import ColorRGBA
from pymessages.std_msgs.msg.header import Header
from pymessages.visualization_msgs.msg.mesh_file import MeshFile
from pymessages.visualization_msgs.msg.uv_coordinate import UVCoordinate

class Marker(Msg):
    ARROW = 0
    CUBE = 1
    SPHERE = 2
    CYLINDER = 3
    LINE_STRIP = 4
    LINE_LIST = 5
    CUBE_LIST = 6
    SPHERE_LIST = 7
    POINTS = 8
    TEXT_VIEW_FACING = 9
    MESH_RESOURCE = 10
    TRIANGLE_LIST = 11
    ARROW_STRIP = 12
    ADD = 0
    MODIFY = 0
    DELETE = 2
    DELETEALL = 3
    _fields = [
        ('header', f_msg(Header)),
        ('ns', f_string()),
        ('id', f_scalar('i')),
        ('type', f_scalar('i')),
        ('action', f_scalar('i')),
        ('pose', f_msg(Pose)),
        ('scale', f_msg(Vector3)),
        ('color', f_msg(ColorRGBA)),
        ('lifetime', f_msg(Duration)),
        ('frame_locked', f_bool()),
        ('points', f_seq(f_msg(Point))),
        ('colors', f_seq(f_msg(ColorRGBA))),
        ('texture_resource', f_string()),
        ('texture', f_msg(CompressedImage)),
        ('uv_coordinates', f_seq(f_msg(UVCoordinate))),
        ('text', f_string()),
        ('mesh_resource', f_string()),
        ('mesh_file', f_msg(MeshFile)),
        ('mesh_use_embedded_materials', f_bool()),
    ]

    def __init__(self):
        self.header = Header()
        self.ns = ""
        self.id = 0
        self.type = 0
        self.action = 0
        self.pose = Pose()
        self.scale = Vector3()
        self.color = ColorRGBA()
        self.lifetime = Duration()
        self.frame_locked = False
        self.points = []
        self.colors = []
        self.texture_resource = ""
        self.texture = CompressedImage()
        self.uv_coordinates = []
        self.text = ""
        self.mesh_resource = ""
        self.mesh_file = MeshFile()
        self.mesh_use_embedded_materials = False
