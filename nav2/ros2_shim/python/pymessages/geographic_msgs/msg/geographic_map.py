from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.bounding_box import BoundingBox
from pymessages.geographic_msgs.msg.key_value import KeyValue
from pymessages.geographic_msgs.msg.map_feature import MapFeature
from pymessages.geographic_msgs.msg.way_point import WayPoint
from pymessages.std_msgs.msg.header import Header
from pymessages.unique_identifier_msgs.msg.uuid import UUID

class GeographicMap(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('id', f_msg(UUID)),
        ('bounds', f_msg(BoundingBox)),
        ('points', f_seq(f_msg(WayPoint))),
        ('features', f_seq(f_msg(MapFeature))),
        ('props', f_seq(f_msg(KeyValue))),
    ]

    def __init__(self):
        self.header = Header()
        self.id = UUID()
        self.bounds = BoundingBox()
        self.points = []
        self.features = []
        self.props = []
