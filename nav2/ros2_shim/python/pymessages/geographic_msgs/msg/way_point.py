from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.geo_point import GeoPoint
from pymessages.geographic_msgs.msg.key_value import KeyValue
from pymessages.unique_identifier_msgs.msg.uuid import UUID

class WayPoint(Msg):
    _fields = [
        ('id', f_msg(UUID)),
        ('position', f_msg(GeoPoint)),
        ('props', f_seq(f_msg(KeyValue))),
    ]

    def __init__(self):
        self.id = UUID()
        self.position = GeoPoint()
        self.props = []
