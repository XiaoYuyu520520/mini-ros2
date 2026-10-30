from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.sensor_msgs.msg.joy_feedback import JoyFeedback

class JoyFeedbackArray(Msg):
    _fields = [
        ('array', f_seq(f_msg(JoyFeedback))),
    ]

    def __init__(self):
        self.array = []
