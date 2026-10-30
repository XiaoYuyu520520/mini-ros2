from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class LoggerLevel(Msg):
    LOG_LEVEL_UNKNOWN = 0
    LOG_LEVEL_DEBUG = 10
    LOG_LEVEL_INFO = 20
    LOG_LEVEL_WARN = 30
    LOG_LEVEL_ERROR = 40
    LOG_LEVEL_FATAL = 50
    _fields = [
        ('name', f_string()),
        ('level', f_scalar('I')),
    ]

    def __init__(self):
        self.name = ""
        self.level = 0
