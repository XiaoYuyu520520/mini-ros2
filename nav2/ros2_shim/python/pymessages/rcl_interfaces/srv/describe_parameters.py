from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.rcl_interfaces.msg.parameter_descriptor import ParameterDescriptor

class DescribeParameters:

    class Request(Msg):
        _fields = [
            ('names', f_seq(f_string())),
        ]

        def __init__(self):
            self.names = []

    class Response(Msg):
        _fields = [
            ('descriptors', f_seq(f_msg(ParameterDescriptor))),
        ]

        def __init__(self):
            self.descriptors = []
