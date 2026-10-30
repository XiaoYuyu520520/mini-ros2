from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav_msgs.msg.occupancy_grid import OccupancyGrid

class ProjectedMap(Msg):
    _fields = [
        ('map', f_msg(OccupancyGrid)),
        ('min_z', f_scalar('d')),
        ('max_z', f_scalar('d')),
    ]

    def __init__(self):
        self.map = OccupancyGrid()
        self.min_z = 0.0
        self.max_z = 0.0
