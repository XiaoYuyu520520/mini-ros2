from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.lifecycle_msgs.msg.state import State

class GetState:

    class Request(Msg):

        def __init__(self):
            pass

    class Response(Msg):
        _fields = [
            ('current_state', f_msg(State)),
        ]

        def __init__(self):
            self.current_state = State()
