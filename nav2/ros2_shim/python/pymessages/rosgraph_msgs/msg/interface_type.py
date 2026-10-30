from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.rosgraph_msgs.msg.type_hash import TypeHash

class InterfaceType(Msg):
    _fields = [
        ('name', f_string()),
        ('hash', f_msg(TypeHash)),
    ]

    def __init__(self):
        self.name = ""
        self.hash = TypeHash()
