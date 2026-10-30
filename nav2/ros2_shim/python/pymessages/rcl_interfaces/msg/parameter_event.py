from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.time import Time
from pymessages.rcl_interfaces.msg.parameter import Parameter

class ParameterEvent(Msg):
    _fields = [
        ('stamp', f_msg(Time)),
        ('node', f_string()),
        ('new_parameters', f_seq(f_msg(Parameter))),
        ('changed_parameters', f_seq(f_msg(Parameter))),
        ('deleted_parameters', f_seq(f_msg(Parameter))),
    ]

    def __init__(self):
        self.stamp = Time()
        self.node = ""
        self.new_parameters = []
        self.changed_parameters = []
        self.deleted_parameters = []
