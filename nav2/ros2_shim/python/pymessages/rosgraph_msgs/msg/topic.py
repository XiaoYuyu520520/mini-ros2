from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.rosgraph_msgs.msg.interface_type import InterfaceType
from pymessages.rosgraph_msgs.msg.qo_s_profile import QoSProfile

class Topic(Msg):
    _fields = [
        ('name', f_string()),
        ('type', f_msg(InterfaceType)),
        ('qos', f_msg(QoSProfile)),
    ]

    def __init__(self):
        self.name = ""
        self.type = InterfaceType()
        self.qos = QoSProfile()
