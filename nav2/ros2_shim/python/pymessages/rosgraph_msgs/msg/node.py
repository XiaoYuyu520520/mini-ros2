from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.rcl_interfaces.msg.parameter_descriptor import ParameterDescriptor
from pymessages.rcl_interfaces.msg.parameter_value import ParameterValue
from pymessages.rosgraph_msgs.msg.action import Action
from pymessages.rosgraph_msgs.msg.service import Service
from pymessages.rosgraph_msgs.msg.topic import Topic

class Node(Msg):
    _fields = [
        ('name', f_string()),
        ('parameters', f_seq(f_msg(ParameterDescriptor))),
        ('parameter_values', f_seq(f_msg(ParameterValue))),
        ('publishers', f_seq(f_msg(Topic))),
        ('subscriptions', f_seq(f_msg(Topic))),
        ('service_clients', f_seq(f_msg(Service))),
        ('service_servers', f_seq(f_msg(Service))),
        ('action_clients', f_seq(f_msg(Action))),
        ('action_servers', f_seq(f_msg(Action))),
    ]

    def __init__(self):
        self.name = ""
        self.parameters = []
        self.parameter_values = []
        self.publishers = []
        self.subscriptions = []
        self.service_clients = []
        self.service_servers = []
        self.action_clients = []
        self.action_servers = []
