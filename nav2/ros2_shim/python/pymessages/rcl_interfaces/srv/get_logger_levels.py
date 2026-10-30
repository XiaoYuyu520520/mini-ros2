from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.rcl_interfaces.msg.logger_level import LoggerLevel

class GetLoggerLevels:

    class Request(Msg):
        _fields = [
            ('names', f_seq(f_string())),
        ]

        def __init__(self):
            self.names = []

    class Response(Msg):
        _fields = [
            ('levels', f_seq(f_msg(LoggerLevel))),
        ]

        def __init__(self):
            self.levels = []
