from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.rcl_interfaces.msg.parameter_value import ParameterValue

class Parameter(Msg):
    _fields = [
        ('name', f_string()),
        ('value', f_msg(ParameterValue)),
    ]

    def __init__(self):
        self.name = ""
        self.value = ParameterValue()
