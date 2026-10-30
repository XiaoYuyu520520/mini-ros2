from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class JoyFeedback(Msg):
    TYPE_LED = 0
    TYPE_RUMBLE = 1
    TYPE_BUZZER = 2
    _fields = [
        ('type', f_scalar('B')),
        ('id', f_scalar('B')),
        ('intensity', f_scalar('f')),
    ]

    def __init__(self):
        self.type = 0
        self.id = 0
        self.intensity = 0.0
