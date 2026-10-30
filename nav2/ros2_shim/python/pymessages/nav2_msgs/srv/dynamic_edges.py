from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav2_msgs.msg.edge_cost import EdgeCost

class DynamicEdges:

    class Request(Msg):
        _fields = [
            ('closed_edges', f_seq(f_scalar('H'))),
            ('opened_edges', f_seq(f_scalar('H'))),
            ('adjust_edges', f_seq(f_msg(EdgeCost))),
        ]

        def __init__(self):
            self.closed_edges = []
            self.opened_edges = []
            self.adjust_edges = []

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
        ]

        def __init__(self):
            self.success = False
