from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.lifecycle_msgs.msg.state import State

class GetAvailableStates:

    class Request(Msg):

        def __init__(self):
            pass

    class Response(Msg):
        _fields = [
            ('available_states', f_seq(f_msg(State))),
        ]

        def __init__(self):
            self.available_states = []
