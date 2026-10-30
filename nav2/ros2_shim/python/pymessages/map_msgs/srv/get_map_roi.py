from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav_msgs.msg.occupancy_grid import OccupancyGrid

class GetMapROI:

    class Request(Msg):
        _fields = [
            ('x', f_scalar('d')),
            ('y', f_scalar('d')),
            ('l_x', f_scalar('d')),
            ('l_y', f_scalar('d')),
        ]

        def __init__(self):
            self.x = 0.0
            self.y = 0.0
            self.l_x = 0.0
            self.l_y = 0.0

    class Response(Msg):
        _fields = [
            ('sub_map', f_msg(OccupancyGrid)),
        ]

        def __init__(self):
            self.sub_map = OccupancyGrid()
