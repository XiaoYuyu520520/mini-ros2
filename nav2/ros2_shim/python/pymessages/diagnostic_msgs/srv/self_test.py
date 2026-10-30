from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.diagnostic_msgs.msg.diagnostic_status import DiagnosticStatus

class SelfTest:

    class Request(Msg):

        def __init__(self):
            pass

    class Response(Msg):
        _fields = [
            ('id', f_string()),
            ('passed', f_scalar('B')),
            ('status', f_seq(f_msg(DiagnosticStatus))),
        ]

        def __init__(self):
            self.id = ""
            self.passed = 0
            self.status = []
