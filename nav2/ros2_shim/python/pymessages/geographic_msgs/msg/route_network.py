from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.bounding_box import BoundingBox
from pymessages.geographic_msgs.msg.key_value import KeyValue
from pymessages.geographic_msgs.msg.route_segment import RouteSegment
from pymessages.geographic_msgs.msg.way_point import WayPoint
from pymessages.std_msgs.msg.header import Header
from pymessages.unique_identifier_msgs.msg.uuid import UUID

class RouteNetwork(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('id', f_msg(UUID)),
        ('bounds', f_msg(BoundingBox)),
        ('points', f_seq(f_msg(WayPoint))),
        ('segments', f_seq(f_msg(RouteSegment))),
        ('props', f_seq(f_msg(KeyValue))),
    ]

    def __init__(self):
        self.header = Header()
        self.id = UUID()
        self.bounds = BoundingBox()
        self.points = []
        self.segments = []
        self.props = []
