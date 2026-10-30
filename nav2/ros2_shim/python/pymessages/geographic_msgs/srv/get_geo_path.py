from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.geo_path import GeoPath
from pymessages.geographic_msgs.msg.geo_point import GeoPoint
from pymessages.unique_identifier_msgs.msg.uuid import UUID

class GetGeoPath:

    class Request(Msg):
        _fields = [
            ('start', f_msg(GeoPoint)),
            ('goal', f_msg(GeoPoint)),
        ]

        def __init__(self):
            self.start = GeoPoint()
            self.goal = GeoPoint()

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
            ('status', f_string()),
            ('plan', f_msg(GeoPath)),
            ('network', f_msg(UUID)),
            ('start_seg', f_msg(UUID)),
            ('goal_seg', f_msg(UUID)),
            ('distance', f_scalar('d')),
        ]

        def __init__(self):
            self.success = False
            self.status = ""
            self.plan = GeoPath()
            self.network = UUID()
            self.start_seg = UUID()
            self.goal_seg = UUID()
            self.distance = 0.0
