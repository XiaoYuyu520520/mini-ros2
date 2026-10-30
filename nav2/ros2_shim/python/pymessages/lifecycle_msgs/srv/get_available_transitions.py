from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.lifecycle_msgs.msg.transition_description import TransitionDescription

class GetAvailableTransitions:

    class Request(Msg):

        def __init__(self):
            pass

    class Response(Msg):
        _fields = [
            ('available_transitions', f_seq(f_msg(TransitionDescription))),
        ]

        def __init__(self):
            self.available_transitions = []
