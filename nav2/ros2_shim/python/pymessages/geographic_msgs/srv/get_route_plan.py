from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.geographic_msgs.msg.route_path import RoutePath
from pymessages.unique_identifier_msgs.msg.uuid import UUID

class GetRoutePlan:

    class Request(Msg):
        _fields = [
            ('network', f_msg(UUID)),
            ('start', f_msg(UUID)),
            ('goal', f_msg(UUID)),
        ]

        def __init__(self):
            self.network = UUID()
            self.start = UUID()
            self.goal = UUID()

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
            ('status', f_string()),
            ('plan', f_msg(RoutePath)),
        ]

        def __init__(self):
            self.success = False
            self.status = ""
            self.plan = RoutePath()
