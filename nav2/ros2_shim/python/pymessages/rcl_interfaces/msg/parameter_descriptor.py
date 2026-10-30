from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.rcl_interfaces.msg.floating_point_range import FloatingPointRange
from pymessages.rcl_interfaces.msg.integer_range import IntegerRange

class ParameterDescriptor(Msg):
    _fields = [
        ('name', f_string()),
        ('type', f_scalar('B')),
        ('description', f_string()),
        ('additional_constraints', f_string()),
        ('read_only', f_bool()),
        ('dynamic_typing', f_bool()),
        ('floating_point_range', f_seq(f_msg(FloatingPointRange))),
        ('integer_range', f_seq(f_msg(IntegerRange))),
    ]

    def __init__(self):
        self.name = ""
        self.type = 0
        self.description = ""
        self.additional_constraints = ""
        self.read_only = False
        self.dynamic_typing = False
        self.floating_point_range = []
        self.integer_range = []
