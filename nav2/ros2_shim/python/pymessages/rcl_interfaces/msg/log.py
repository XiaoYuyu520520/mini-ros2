from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.time import Time

class Log(Msg):
    DEBUG = 10
    INFO = 20
    WARN = 30
    ERROR = 40
    FATAL = 50
    _fields = [
        ('stamp', f_msg(Time)),
        ('level', f_scalar('B')),
        ('name', f_string()),
        ('msg', f_string()),
        ('file', f_string()),
        ('function', f_string()),
        ('line', f_scalar('I')),
    ]

    def __init__(self):
        self.stamp = Time()
        self.level = 0
        self.name = ""
        self.msg = ""
        self.file = ""
        self.function = ""
        self.line = 0
