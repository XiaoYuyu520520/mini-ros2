from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.sensor_msgs.msg.point_cloud2 import PointCloud2

class GetPointMap:

    class Request(Msg):

        def __init__(self):
            pass

    class Response(Msg):
        _fields = [
            ('map', f_msg(PointCloud2)),
        ]

        def __init__(self):
            self.map = PointCloud2()
