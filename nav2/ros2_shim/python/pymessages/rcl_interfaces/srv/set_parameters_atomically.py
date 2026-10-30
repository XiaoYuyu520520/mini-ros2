from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.rcl_interfaces.msg.parameter import Parameter
from pymessages.rcl_interfaces.msg.set_parameters_result import SetParametersResult

class SetParametersAtomically:

    class Request(Msg):
        _fields = [
            ('parameters', f_seq(f_msg(Parameter))),
        ]

        def __init__(self):
            self.parameters = []

    class Response(Msg):
        _fields = [
            ('result', f_msg(SetParametersResult)),
        ]

        def __init__(self):
            self.result = SetParametersResult()
