from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.rcl_interfaces.msg.parameter_descriptor import ParameterDescriptor

class ParameterEventDescriptors(Msg):
    _fields = [
        ('new_parameters', f_seq(f_msg(ParameterDescriptor))),
        ('changed_parameters', f_seq(f_msg(ParameterDescriptor))),
        ('deleted_parameters', f_seq(f_msg(ParameterDescriptor))),
    ]

    def __init__(self):
        self.new_parameters = []
        self.changed_parameters = []
        self.deleted_parameters = []
