from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geometry_msgs.msg.pose_with_covariance_stamped import PoseWithCovarianceStamped
from pymessages.nav_msgs.msg.occupancy_grid import OccupancyGrid

class SetMap:

    class Request(Msg):
        _fields = [
            ('map', f_msg(OccupancyGrid)),
            ('initial_pose', f_msg(PoseWithCovarianceStamped)),
        ]

        def __init__(self):
            self.map = OccupancyGrid()
            self.initial_pose = PoseWithCovarianceStamped()

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
        ]

        def __init__(self):
            self.success = False
