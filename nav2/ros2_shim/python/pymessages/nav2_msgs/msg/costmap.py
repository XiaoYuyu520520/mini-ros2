from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav2_msgs.msg.costmap_meta_data import CostmapMetaData
from pymessages.std_msgs.msg.header import Header

class Costmap(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('metadata', f_msg(CostmapMetaData)),
        ('data', f_seq(f_scalar('B'))),
    ]

    def __init__(self):
        self.header = Header()
        self.metadata = CostmapMetaData()
        self.data = []
