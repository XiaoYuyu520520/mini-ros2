from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.rosgraph_msgs.msg.node import Node

class Graph(Msg):
    _fields = [
        ('nodes', f_seq(f_msg(Node))),
    ]

    def __init__(self):
        self.nodes = []
