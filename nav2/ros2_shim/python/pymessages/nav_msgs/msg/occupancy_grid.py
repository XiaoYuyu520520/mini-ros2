from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav_msgs.msg.map_meta_data import MapMetaData
from pymessages.std_msgs.msg.header import Header

class OccupancyGrid(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('info', f_msg(MapMetaData)),
        ('data', f_seq(f_scalar('b'))),
    ]

    def __init__(self):
        self.header = Header()
        self.info = MapMetaData()
        self.data = []
