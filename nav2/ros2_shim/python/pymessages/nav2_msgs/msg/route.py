from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav2_msgs.msg.route_edge import RouteEdge
from pymessages.nav2_msgs.msg.route_node import RouteNode
from pymessages.std_msgs.msg.header import Header

class Route(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('route_cost', f_scalar('f')),
        ('nodes', f_seq(f_msg(RouteNode))),
        ('edges', f_seq(f_msg(RouteEdge))),
    ]

    def __init__(self):
        self.header = Header()
        self.route_cost = 0.0
        self.nodes = []
        self.edges = []
