from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.lifecycle_msgs.msg.transition import Transition

class ChangeState:

    class Request(Msg):
        _fields = [
            ('transition', f_msg(Transition)),
        ]

        def __init__(self):
            self.transition = Transition()

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
        ]

        def __init__(self):
            self.success = False
