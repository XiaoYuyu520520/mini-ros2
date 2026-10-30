from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.rcl_interfaces.msg.list_parameters_result import ListParametersResult

class ListParameters:

    class Request(Msg):
        DEPTH_RECURSIVE = 0
        _fields = [
            ('prefixes', f_seq(f_string())),
            ('depth', f_scalar('Q')),
        ]

        def __init__(self):
            self.prefixes = []
            self.depth = 0

    class Response(Msg):
        _fields = [
            ('result', f_msg(ListParametersResult)),
        ]

        def __init__(self):
            self.result = ListParametersResult()
