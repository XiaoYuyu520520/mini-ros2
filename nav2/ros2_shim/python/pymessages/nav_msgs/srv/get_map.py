from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav_msgs.msg.occupancy_grid import OccupancyGrid

class GetMap:

    class Request(Msg):

        def __init__(self):
            pass

    class Response(Msg):
        _fields = [
            ('map', f_msg(OccupancyGrid)),
        ]

        def __init__(self):
            self.map = OccupancyGrid()
