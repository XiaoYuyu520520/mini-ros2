from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.lifecycle_msgs.msg.state import State
from pymessages.lifecycle_msgs.msg.transition import Transition

class TransitionDescription(Msg):
    _fields = [
        ('transition', f_msg(Transition)),
        ('start_state', f_msg(State)),
        ('goal_state', f_msg(State)),
    ]

    def __init__(self):
        self.transition = Transition()
        self.start_state = State()
        self.goal_state = State()
