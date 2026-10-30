from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav_msgs.msg.occupancy_grid import OccupancyGrid

class LoadMap:

    class Request(Msg):
        _fields = [
            ('map_url', f_string()),
        ]

        def __init__(self):
            self.map_url = ""

    class Response(Msg):
        RESULT_SUCCESS = 0
        RESULT_MAP_DOES_NOT_EXIST = 1
        RESULT_INVALID_MAP_DATA = 2
        RESULT_INVALID_MAP_METADATA = 3
        RESULT_UNDEFINED_FAILURE = 255
        _fields = [
            ('map', f_msg(OccupancyGrid)),
            ('result', f_scalar('B')),
        ]

        def __init__(self):
            self.map = OccupancyGrid()
            self.result = 0
