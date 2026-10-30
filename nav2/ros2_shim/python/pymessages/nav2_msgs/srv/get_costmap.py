from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav2_msgs.msg.costmap import Costmap
from pymessages.nav2_msgs.msg.costmap_meta_data import CostmapMetaData

class GetCostmap:

    class Request(Msg):
        _fields = [
            ('specs', f_msg(CostmapMetaData)),
        ]

        def __init__(self):
            self.specs = CostmapMetaData()

    class Response(Msg):
        _fields = [
            ('map', f_msg(Costmap)),
        ]

        def __init__(self):
            self.map = Costmap()
