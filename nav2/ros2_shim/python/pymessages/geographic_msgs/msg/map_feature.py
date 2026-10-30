from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.key_value import KeyValue
from pymessages.unique_identifier_msgs.msg.uuid import UUID

class MapFeature(Msg):
    _fields = [
        ('id', f_msg(UUID)),
        ('components', f_seq(f_msg(UUID))),
        ('props', f_seq(f_msg(KeyValue))),
    ]

    def __init__(self):
        self.id = UUID()
        self.components = []
        self.props = []
