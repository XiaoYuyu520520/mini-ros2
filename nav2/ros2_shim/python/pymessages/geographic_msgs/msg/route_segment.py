from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.key_value import KeyValue
from pymessages.unique_identifier_msgs.msg.uuid import UUID

class RouteSegment(Msg):
    _fields = [
        ('id', f_msg(UUID)),
        ('start', f_msg(UUID)),
        ('end', f_msg(UUID)),
        ('props', f_seq(f_msg(KeyValue))),
    ]

    def __init__(self):
        self.id = UUID()
        self.start = UUID()
        self.end = UUID()
        self.props = []
