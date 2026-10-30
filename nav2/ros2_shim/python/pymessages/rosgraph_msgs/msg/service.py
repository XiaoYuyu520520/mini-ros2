from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.rosgraph_msgs.msg.interface_type import InterfaceType
from pymessages.rosgraph_msgs.msg.qo_s_profile import QoSProfile

class Service(Msg):
    _fields = [
        ('name', f_string()),
        ('request_type', f_msg(InterfaceType)),
        ('request_qos', f_msg(QoSProfile)),
        ('response_type', f_msg(InterfaceType)),
        ('response_qos', f_msg(QoSProfile)),
    ]

    def __init__(self):
        self.name = ""
        self.request_type = InterfaceType()
        self.request_qos = QoSProfile()
        self.response_type = InterfaceType()
        self.response_qos = QoSProfile()
