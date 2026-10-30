from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.key_value import KeyValue
from pymessages.std_msgs.msg.header import Header
from pymessages.unique_identifier_msgs.msg.uuid import UUID

class RoutePath(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('network', f_msg(UUID)),
        ('segments', f_seq(f_msg(UUID))),
        ('props', f_seq(f_msg(KeyValue))),
    ]

    def __init__(self):
        self.header = Header()
        self.network = UUID()
        self.segments = []
        self.props = []
