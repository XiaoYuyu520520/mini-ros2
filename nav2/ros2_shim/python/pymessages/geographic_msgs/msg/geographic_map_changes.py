from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.geographic_map import GeographicMap
from pymessages.std_msgs.msg.header import Header
from pymessages.unique_identifier_msgs.msg.uuid import UUID

class GeographicMapChanges(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('diffs', f_msg(GeographicMap)),
        ('deletes', f_seq(f_msg(UUID))),
    ]

    def __init__(self):
        self.header = Header()
        self.diffs = GeographicMap()
        self.deletes = []
